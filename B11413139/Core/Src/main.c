/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 深夜研究室：微波爐泡麵保衛戰 (Instant Noodle Defense System)
  *                   國立雲林科技大學 電子工程系 微計算機原理及應用 期中專題
  * @target         : NUCLEO-G474RE (170 MHz Boost Mode, CMSIS 暫存器架構)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE,       // 待機閒置（綠燈呼吸，微波爐可為空，待上鎖）
    STATE_ARMED,      // 受護上鎖（泡麵在位且已鎖定，綠燈閃爍；強拔或開門直接警報）
    STATE_UNLOCKED,   // 已授權解鎖（密碼正確，綠燈恆亮，允許開門與拔起 PA1）
    STATE_ALARM       // 遭劫警報（未解鎖開門、未解鎖拔麵或錯3次，紅燈長亮+蜂鳴器狂鳴）
} SystemState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RING_BUFFER_SIZE       64
#define MAX_PASSWORD_ATTEMPTS  3
#define CMD_BUFFER_SIZE        32
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile SystemState current_state = STATE_IDLE;
volatile SystemState last_state = (SystemState)-1;

// 密碼與防暴力破解
char stored_password[16] = "12345";
uint8_t err_count = 0;

// 按鍵去彈跳時間戳 (PC13)
volatile uint32_t last_exti_tick = 0;

// USART2 環形緩衝區 (Ring Buffer)
volatile char rx_buffer[RING_BUFFER_SIZE];
volatile uint8_t rx_head = 0;
volatile uint8_t rx_tail = 0;

// 軟體擬似 PWM 呼吸燈變數 (PA5)
uint16_t breath_duty = 0;
int8_t   breath_dir = 1;
uint32_t last_breath_tick = 0;

// 閃爍計時變數 (PA5)
uint32_t last_blink_tick = 0;
uint8_t  blink_led_state = 0;

// 警報狀態聲音計時變數 (PB1)
uint32_t last_alarm_sound_tick = 0;
uint8_t  alarm_beep_toggle = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void UART_Send_Char(char c);
void UART_Send_String(const char *str);
void Buzzer_Beep(uint16_t duration_ms);
void Buzzer_DoubleBeep(void);
void Process_CLI_Command(char *cmd);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief EXTI 中斷回呼函式：處理 PC13 開門事件 (門開關替身)
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_13) {
        uint32_t now = HAL_GetTick();
        if ((now - last_exti_tick) > 200) {
            last_exti_tick = now;

            if (current_state == STATE_ARMED) {
                // 守護中強行開門 -> 遭劫警報！
                current_state = STATE_ALARM;
            } else if (current_state == STATE_UNLOCKED) {
                // 授權解鎖後開門 -> 提示取餐
                UART_Send_String("\r\n[DOOR] 主人已開門，請取出泡麵（斷開 PA1 杜邦線）。\r\n");
            }
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  // 1. 確保 GPIOB 時脈致能，並以暫存器配置 PB0(紅燈) 與 PB1(蜂鳴器) 為推挽輸出
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;

  GPIOB->MODER &= ~((3U << (0 * 2)) | (3U << (1 * 2)));
  GPIOB->MODER |=  ((1U << (0 * 2)) | (1U << (1 * 2)));
  GPIOB->OTYPER &= ~((1U << 0) | (1U << 1));
  GPIOB->OSPEEDR |= ((3U << (0 * 2)) | (3U << (1 * 2)));
  GPIOB->BSRR = (1U << (0 + 16)) | (1U << (1 + 16));     // 預設關閉紅燈與蜂鳴器

  // 2. 確保 PA1 內部上拉電阻致能
  GPIOA->PUPDR &= ~(3U << (1 * 2));
  GPIOA->PUPDR |=  (1U << (1 * 2));

  // 3. 致能 USART2 接收中斷 (RXNE) 於暫存器層級
  USART2->CR1 |= USART_CR1_RXNEIE_RXFNEIE;

  char cmd_line[CMD_BUFFER_SIZE];
  uint8_t cmd_idx = 0;

  // 開機歡迎訊息
  UART_Send_String("\r\n==============================================\r\n");
  UART_Send_String(" 深夜研究室：微波爐泡麵保衛戰 (SYSCLK: 170MHz)\r\n");
  UART_Send_String("==============================================\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* ------------------ 1. 狀態切換介面提示 ------------------ */
    if (current_state != last_state) {
        last_state = current_state;
        switch (current_state) {
            case STATE_IDLE:
                GPIOB->BSRR = (1U << (0 + 16)) | (1U << (1 + 16));
                UART_Send_String("\r\n[STATUS: IDLE] 微波爐待機中（綠燈呼吸）\r\n");
                UART_Send_String("[PROMPT] 請放進泡麵(接上PA1杜邦線接地)並輸入: LOCK <密碼>\r\n> ");
                break;

            case STATE_ARMED:
                GPIOB->BSRR = (1U << (0 + 16)) | (1U << (1 + 16));
                err_count = 0;
                UART_Send_String("\r\n[STATUS: ARMED] 泡麵守護中（綠燈閃爍）\r\n");
                UART_Send_String("[PROMPT] 取用請輸入解鎖密碼:\r\nEnter Password > ");
                break;

            case STATE_UNLOCKED:
                GPIOB->BSRR = (1U << (0 + 16)) | (1U << (1 + 16));
                GPIOA->BSRR = (1U << 5);
                UART_Send_String("\r\n[STATUS: UNLOCKED] 授權成功！門鎖已解除（綠燈恆亮）。\r\n");
                UART_Send_String("[PROMPT] 請按 PC13 開門並拿走泡麵。\r\n> ");
                break;

            case STATE_ALARM:
                GPIOA->BSRR = (1U << (5 + 16));
                GPIOB->BSRR = (1U << 0);
                UART_Send_String("\r\n##############################################\r\n");
                UART_Send_String("#  [ALERT] 警告！偵測到未授權侵入或宵夜遭劫！  #\r\n");
                UART_Send_String("##############################################\r\n");
                UART_Send_String("[PROMPT] 輸入 RESET 解除警報:\r\n> ");
                break;
        }
    }

    /* ---------------- 2. PA1 實體托盤感應線巡檢 (杜邦線) ---------------- */
    uint8_t item_present = ((GPIOA->IDR & (1U << 1)) == 0) ? 1 : 0;

    if (current_state == STATE_ARMED) {
        // 守護狀態下：只要未解鎖就拔起 PA1 -> 立即觸發警報！
        if (!item_present) {
            current_state = STATE_ALARM;
            UART_Send_String("\r\n[SECURITY BREACH] 守護中未解鎖直接拔起泡麵！觸發警報！\r\n");
        }
    } else if (current_state == STATE_UNLOCKED) {
        // 授權解鎖後：合法拔走泡麵 -> 祝用餐愉快，順暢回到待機
        if (!item_present) {
            UART_Send_String("\r\n[WELCOME] 密碼驗證通過，泡麵已安全取出，祝用餐愉快！\r\n");
            Buzzer_Beep(100);
            current_state = STATE_IDLE;
        }
    }

    /* ------------------ 3. PA5 綠燈視覺效果更新 ------------------ */
    if (current_state == STATE_IDLE) {
        if ((now - last_breath_tick) >= 15) {
            last_breath_tick = now;
            breath_duty += breath_dir;
            if (breath_duty >= 100) { breath_duty = 100; breath_dir = -1; }
            if (breath_duty <= 0)   { breath_duty = 0;   breath_dir = 1;  }
        }
        uint32_t slice = now % 10;
        if (slice < (breath_duty / 10)) {
            GPIOA->BSRR = (1U << 5);
        } else {
            GPIOA->BSRR = (1U << (5 + 16));
        }
    } else if (current_state == STATE_ARMED) {
        if ((now - last_blink_tick) >= 250) {
            last_blink_tick = now;
            blink_led_state = !blink_led_state;
            if (blink_led_state) {
                GPIOA->BSRR = (1U << 5);
            } else {
                GPIOA->BSRR = (1U << (5 + 16));
            }
        }
    }

    /* ---------------- 4. 警報狀態持續狂鳴 (相容有源與無源蜂鳴器) ---------------- */
    if (current_state == STATE_ALARM) {
        if ((now - last_alarm_sound_tick) >= 150) {
            last_alarm_sound_tick = now;
            alarm_beep_toggle = !alarm_beep_toggle;
        }

        if (alarm_beep_toggle) {
            GPIOB->BSRR = (1U << 1);
            for (volatile int d = 0; d < 800; d++);
            GPIOB->BSRR = (1U << (1 + 16));
            for (volatile int d = 0; d < 800; d++);
        } else {
            GPIOB->BSRR = (1U << (1 + 16));
        }
    }

    /* ---------------- 5. UART 指令字串提取與解析 ---------------- */
    while (rx_tail != rx_head) {
        char c = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % RING_BUFFER_SIZE;

        UART_Send_Char(c);

        if (c == '\r' || c == '\n') {
            if (cmd_idx > 0) {
                cmd_line[cmd_idx] = '\0';
                Process_CLI_Command(cmd_line);
                cmd_idx = 0;
            }
        } else if (c == '\b' || c == 0x7F) {
            if (cmd_idx > 0) cmd_idx--;
        } else {
            if (cmd_idx < (CMD_BUFFER_SIZE - 1)) {
                cmd_line[cmd_idx++] = c;
            }
        }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @brief CLI 命令邏輯解析
 */
void Process_CLI_Command(char *cmd) {
    UART_Send_String("\r\n");

    if (strcmp(cmd, "STATUS") == 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[STATUS] 核心:170MHz | 狀態:%d | 托盤:%s\r\n",
                 current_state,
                 ((GPIOA->IDR & (1U << 1)) == 0) ? "泡麵在位" : "空置");
        UART_Send_String(buf);
        return;
    }

    if (strcmp(cmd, "RESET") == 0) {
        current_state = STATE_IDLE;
        err_count = 0;
        GPIOB->BSRR = (1U << (0 + 16)) | (1U << (1 + 16));
        UART_Send_String("[SYSTEM] 系統已重設為待機模式。\r\n");
        return;
    }

    if (current_state == STATE_IDLE) {
        if (strncmp(cmd, "LOCK", 4) == 0) {
            // 上鎖時檢查泡麵是否已放入 (PA1 接地)
            if ((GPIOA->IDR & (1U << 1)) != 0) {
                UART_Send_String("[ERROR] 托盤未感應到泡麵！請先將 PA1 杜邦線插上接地(GND)。\r\n> ");
                return;
            }

            char *p = cmd + 4;
            while (*p == ' ') p++;
            if (strlen(p) > 0) {
                strncpy(stored_password, p, sizeof(stored_password) - 1);
            } else {
                strcpy(stored_password, "12345");
            }

            Buzzer_Beep(100);
            current_state = STATE_ARMED;
        } else {
            UART_Send_String("[HELP] 待機中，請輸入: LOCK <密碼>\r\n> ");
        }
    } else if (current_state == STATE_ARMED) {
        if (strcmp(cmd, stored_password) == 0) {
            Buzzer_Beep(100);
            err_count = 0;
            current_state = STATE_UNLOCKED;
        } else {
            err_count++;
            if (err_count < MAX_PASSWORD_ATTEMPTS) {
                Buzzer_DoubleBeep();
                char err_msg[96];
                snprintf(err_msg, sizeof(err_msg), "[DENIED] 密碼錯誤！剩餘嘗試次數: %d\r\nEnter Password > ",
                         MAX_PASSWORD_ATTEMPTS - err_count);
                UART_Send_String(err_msg);
            } else {
                current_state = STATE_ALARM;
            }
        }
    } else if (current_state == STATE_ALARM) {
        UART_Send_String("[ALERT] 警報狂鳴中！請輸入 RESET 復原系統。\r\n> ");
    }
}

void UART_Send_Char(char c) {
    while (!(USART2->ISR & USART_ISR_TXE_TXFNF));
    USART2->TDR = c;
}

void UART_Send_String(const char *str) {
    while (*str) {
        UART_Send_Char(*str++);
    }
}

void Buzzer_Beep(uint16_t duration_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < duration_ms) {
        GPIOB->BSRR = (1U << 1);
        for (volatile int i = 0; i < 1500; i++);
        GPIOB->BSRR = (1U << (1 + 16));
        for (volatile int i = 0; i < 1500; i++);
    }
}

void Buzzer_DoubleBeep(void) {
    Buzzer_Beep(80);
    HAL_Delay(70);
    Buzzer_Beep(80);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
