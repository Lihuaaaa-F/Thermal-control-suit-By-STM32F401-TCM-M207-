/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : 调温服 V4.1
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// --- 命令类型枚举 (用于解析 M207 协议) ---
enum CMDTYPE
{
    CMDTYPE_SETTING,
    CMDTYPE_POLLING,
    CMDTYPE_SAVING,
    CMDTYPE_CHANGING,
    CMDTYPE_ERROR
};
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAX_RX_LEN 64
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// --- UART 接收相关全局变量 ---
char rx_buffer[MAX_RX_LEN];
uint8_t rx_index = 0;
uint8_t rx_byte;
uint8_t rx_flag = 0;
// 全局变量，记录uart6最后一次收到字节的时间
uint32_t uart6_last_rx_time = 0;
// UART6接收缓冲区大小
#define UART6_RX_BUF_SIZE 256
uint8_t uart6_rx_buf[UART6_RX_BUF_SIZE];
volatile uint16_t uart6_rx_len = 0;       // 已接收数据长度
volatile uint8_t uart6_rx_frame_flag = 0; // 一帧数据接收完成标志
// --- 核心温度数据全局变量 ---
float tc1_water_temp = 0.0f;      // 贴身水箱实际温度 (后台运行，不显示)
float tc2_water_temp = 0.0f;      // 环境水箱实际温度 (后台运行，不显示)
float tc1_water_temp_set = 25.0f; // 贴身水箱设定温度 (可通过电脑指令设置，后台运行)
float tc2_water_temp_set = 25.0f; // 环境水箱设定温度 (可通过电脑指令设置，后台运行)

// --- 调温控制参数 ---
float target_hot = 37.5f;      // 人体过热阈值
float target_cold = 36.5f;     // 人体过冷阈值
float hysteresis = 0.2f;       // 回差防抖
unsigned char display_buf[10]; // 替代 sprintf 的纯净显示缓存
int t_int, t_frac;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void Process_M207_Reply(void);
enum CMDTYPE ProtocolAnalysis(char *comstr, int comstrlength, char *modulestr, char *paramstr, char *valuestr);
void Send_M207_Cmd(char *cmd);
void Process_PCtoM207(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
    MX_I2C1_Init();
    MX_I2C3_Init();
    MX_USART2_UART_Init();
    MX_USART6_UART_Init();
    /* USER CODE BEGIN 2 */

    ///__HAL_UART_ENABLE_IT(&huart6, UART_IT_IDLE);
    HAL_UART_Receive_IT(&huart6, &uart6_rx_buf[0], 1);

    // 1. 初始化 OLED 并清屏
    OLED_Init();
    OLED_CLS();

    // 第一行
    OLED_ShowCN(0, 0, 3);  // 设
    OLED_ShowCN(16, 0, 4); // 定
    OLED_ShowCN(32, 0, 0); // 温
    OLED_ShowCN(48, 0, 1); // 度
    OLED_ShowStr(64, 0, (unsigned char *)"1", 2);
    OLED_ShowStr(72, 0, (unsigned char *)":", 2);
    OLED_ShowCN(112, 0, 7); // ℃

    float temp = tc1_water_temp_set;
    if (temp < 0)
        temp = 0;                        // 负数保护，和参考逻辑完全一致，避免异常负温乱码
    t_int = (int)temp;                   // 拆分整数部分
    t_frac = (int)((temp - t_int) * 10); // 仅取1位小数

    display_buf[0] = (t_int / 10) + '0'; // 整数部分：十位
    display_buf[1] = (t_int % 10) + '0'; // 整数部分：个位
    display_buf[2] = '.';                // 小数点
    display_buf[3] = t_frac + '0';       // 小数部分：仅保留1位

    OLED_ShowStr(80, 0, display_buf, 2);

    // 第二行
    OLED_ShowCN(0, 2, 3);  // 设
    OLED_ShowCN(16, 2, 4); // 定
    OLED_ShowCN(32, 2, 0); // 温
    OLED_ShowCN(48, 2, 1); // 度
    OLED_ShowStr(64, 2, (unsigned char *)"2", 2);
    OLED_ShowStr(72, 2, (unsigned char *)":", 2);
    OLED_ShowCN(112, 2, 7); // ℃
    temp = tc2_water_temp_set;
    if (temp < 0)
        temp = 0;
    t_int = (int)temp;
    t_frac = (int)((temp - t_int) * 10);

    // 填充TC2的显示缓冲区
    display_buf[0] = (t_int / 10) + '0';
    display_buf[1] = (t_int % 10) + '0';
    display_buf[2] = '.';
    display_buf[3] = t_frac + '0';

    // 显示TC2：
    OLED_ShowStr(80, 2, display_buf, 2);

    // 第三行
    OLED_ShowCN(0, 4, 5);  // 实
    OLED_ShowCN(16, 4, 6); // 际
    OLED_ShowCN(32, 4, 0); // 温
    OLED_ShowCN(48, 4, 1); // 度
    OLED_ShowStr(64, 4, (unsigned char *)"1", 2);
    OLED_ShowStr(72, 4, (unsigned char *)":", 2);
    OLED_ShowCN(112, 4, 7); // ℃
    // 第四行
    OLED_ShowCN(0, 6, 5);  // 实
    OLED_ShowCN(16, 6, 6); // 际
    OLED_ShowCN(32, 6, 0); // 温
    OLED_ShowCN(48, 6, 1); // 度
    OLED_ShowStr(64, 6, (unsigned char *)"2", 2);
    OLED_ShowStr(72, 6, (unsigned char *)":", 2);
    OLED_ShowCN(112, 6, 7); // ℃

    // 3. 开启第一次串口中断接收 (准备接收 M207 数据)
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

    // 4. 定义局部测温和纯数学显示变量
    /// uint8_t rx_data[2];
    /// int16_t raw_temp = 0;
    /// float real_temp = 0.0;

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        // ==================== 超时判断帧结束！解决一直等字节的问题 ====================
        if (uart6_rx_frame_flag == 0 && uart6_rx_len > 0)
        {
            // 如果超过10ms没收到新字节，就认为收完了
            if (HAL_GetTick() - uart6_last_rx_time > 10)
            {
                uart6_rx_frame_flag = 1; // 手动标记帧完成
            }
        }

        // ==================== 1. 处理串口残留数据  ====================
        if (rx_flag == 1)
        {
            Process_M207_Reply();
            rx_flag = 0;
            HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
        }

        // ==================== 2. 轮询 M207  ====================
        Send_M207_Cmd("TC1:TCACTTEMP?\r");
        HAL_Delay(50);
        if (rx_flag == 1)
        {
            Process_M207_Reply();
            rx_flag = 0;
            HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
        }

        // --------------------------
        // 1. 处理并显示 TC1 水温
        // --------------------------
        float temp = tc1_water_temp;
        if (temp < 0)
            temp = 0;                        // 负数保护，和参考逻辑完全一致，避免异常负温乱码
        t_int = (int)temp;                   // 拆分整数部分
        t_frac = (int)((temp - t_int) * 10); // 仅取1位小数

        display_buf[0] = (t_int / 10) + '0'; // 整数部分：十位
        display_buf[1] = (t_int % 10) + '0'; // 整数部分：个位
        display_buf[2] = '.';                // 小数点
        display_buf[3] = t_frac + '0';       // 小数部分：仅保留1位

        // 显示TC1：

        OLED_ShowStr(80, 4, display_buf, 2);

        Send_M207_Cmd("TC2:TCACTTEMP?\r");
        HAL_Delay(50);
        if (rx_flag == 1)
        {
            Process_M207_Reply();
            rx_flag = 0;
            HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
        }

        // --------------------------
        // 2. 处理并显示 TC2 水温，完全相同的逻辑
        // --------------------------
        temp = tc2_water_temp;
        if (temp < 0)
            temp = 0;
        t_int = (int)temp;
        t_frac = (int)((temp - t_int) * 10);

        // 填充TC2的显示缓冲区
        display_buf[0] = (t_int / 10) + '0';
        display_buf[1] = (t_int % 10) + '0';
        display_buf[2] = '.';
        display_buf[3] = t_frac + '0';

        OLED_ShowStr(80, 6, display_buf, 2);

        if (uart6_rx_frame_flag == 1)
        {
            // 1. 数据预处理：小写转大写
            for (uint16_t i = 0; i < uart6_rx_len; i++)
            {
                if (uart6_rx_buf[i] >= 'a' && uart6_rx_buf[i] <= 'z')
                {
                    uart6_rx_buf[i] -= 0x20;
                }
            }

            // 2. 添加结束符 0x0D (\r)
            if (uart6_rx_len < UART6_RX_BUF_SIZE - 1)
            {
                uart6_rx_buf[uart6_rx_len] = 0x0D;
                uart6_rx_len++;
            }

            // ========== 处理业务 ==========
            Process_PCtoM207();

            // ========== 发送前清错误，避免卡死 ==========
            if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE) != RESET)
            {
                __HAL_UART_CLEAR_OREFLAG(&huart2);
            }
            HAL_UART_Transmit(&huart2, uart6_rx_buf, uart6_rx_len, 100);

            // ========== 重置状态 ==========
            uart6_rx_len = 0;
            uart6_rx_frame_flag = 0;
        }

        HAL_Delay(250); // 补齐循环时间
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

    /** Configure the main internal regulator output voltage
     */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 84;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
// ==================== 1. 自定义原生串口发送函数 ====================
void Send_M207_Cmd(char *cmd)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 10);
}

// ==================== 2. 串口接收中断回调函数 ====================
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        rx_buffer[rx_index++] = rx_byte;
        if (rx_byte == '\r' || rx_index >= MAX_RX_LEN - 1)
        {
            rx_buffer[rx_index] = '\0';
            rx_flag = 1;
            rx_index = 0;
        }
        else
        {
            HAL_UART_Receive_IT(huart, &rx_byte, 1);
        }
    }
    if (huart->Instance == USART6)
    {
        // 溢出保护
        if (uart6_rx_len < UART6_RX_BUF_SIZE - 1)
        {
            uart6_rx_len++;
        }
        // 重点！每收到一个字节，就更新最后接收时间！
        uart6_last_rx_time = HAL_GetTick();

        // 自动开启下一个字节的接收
        HAL_UART_Receive_IT(&huart6, &uart6_rx_buf[uart6_rx_len], 1);
    }
}

/**
 * @brief  重写USART6的中断服务函数
 * @param  None
 * @retval None
 */
void USART6_IRQHandler(void)
{
    // ==================== 1. 先处理溢出错误，避免锁死 ====================
    // 这是你之前卡住的核心原因：溢出错误会导致空闲中断再也触发不了
    if (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart6); // 清溢出错误标志
        // 出错了直接重置接收状态，避免len一直涨
        uart6_rx_len = 0;
        uart6_rx_frame_flag = 0;
        // 重新初始化接收
        HAL_UART_Receive_IT(&huart6, &uart6_rx_buf[0], 1);
        return;
    }

    // ==================== 2. 先检测空闲中断，再调用HAL处理 ====================
    // （调整顺序，避免HAL处理把空闲标志清掉）
    if (__HAL_UART_GET_FLAG(&huart6, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart6); // 清空闲中断标志

        // ==================== 加溢出保护！解决len一直涨的问题 ====================
        if (uart6_rx_len >= UART6_RX_BUF_SIZE - 1)
        {
            // 如果缓冲区满了，直接重置，避免len一直加
            uart6_rx_len = 0;
            uart6_rx_frame_flag = 0;
        }
        else
        {
            // 正常标记帧完成
            uart6_rx_frame_flag = 1;
        }
    }

    // ==================== 3. 最后调用HAL标准处理 ====================
    HAL_UART_IRQHandler(&huart6);
}
// ==================== 3. 核心协议解析函数 ====================
enum CMDTYPE ProtocolAnalysis(char *comstr, int comstrlength, char *modulestr, char *paramstr, char *valuestr)
{
    int ix;
    uint8_t colonflag = 0;
    uint8_t modulelength = 0, paramlength = 0, valuelength = 0;
    enum CMDTYPE cmdtype = CMDTYPE_ERROR;

    for (ix = 0; ix < comstrlength; ix++)
    {
        switch (comstr[ix])
        {
        case ':':
            modulelength = ix;
            colonflag = 1;
            break;
        case '?':
            paramlength = ix;
            cmdtype = CMDTYPE_POLLING;
            break;
        case '=':
            paramlength = ix;
            cmdtype = CMDTYPE_SETTING;
            break;
        }
    }

    if (colonflag != 1 || cmdtype == CMDTYPE_ERROR)
        return CMDTYPE_ERROR;

    valuelength = (comstrlength - 1) - paramlength - 1;
    paramlength = paramlength - modulelength - 1;

    for (ix = 0; ix < modulelength; ix++)
        modulestr[ix] = comstr[ix];
    modulestr[modulelength] = '\0';

    for (ix = 0; ix < paramlength; ix++)
        paramstr[ix] = comstr[ix + 1 + modulelength];
    paramstr[paramlength] = '\0';

    if (cmdtype == CMDTYPE_SETTING)
    {
        for (ix = 0; ix < valuelength; ix++)
            valuestr[ix] = comstr[ix + 1 + modulelength + 1 + paramlength];
        valuestr[valuelength] = '\0';
    }
    return cmdtype;
}

// ==================== 4. 数据包清洗与变量更新函数 ====================
void Process_M207_Reply(void)
{
    char modulestr[20] = {0};
    char paramstr[20] = {0};
    char valuestr[20] = {0};

    int len = strlen(rx_buffer);

    if (ProtocolAnalysis(rx_buffer, len, modulestr, paramstr, valuestr) == CMDTYPE_SETTING)
    {
        if (strcmp(paramstr, "TCACTTEMP") == 0)
        {
            float temp_val = atof(valuestr);
            if (strcmp(modulestr, "TC1") == 0)
                tc1_water_temp = temp_val;

            else if (strcmp(modulestr, "TC2") == 0)
                tc2_water_temp = temp_val;
        }
    }
}
void Process_PCtoM207(void)
{
    char modulestr[20] = {0};
    char paramstr[20] = {0};
    char valuestr[20] = {0};

    int len = strlen((char *)uart6_rx_buf);

    if (ProtocolAnalysis((char *)uart6_rx_buf, len, modulestr, paramstr, valuestr) == CMDTYPE_SETTING)
    {
        if (strcmp(paramstr, "TCADJTEMP") == 0)
        {
            float temp_val_set = atof(valuestr);
            if (strcmp(modulestr, "TC1") == 0)
            {
                tc1_water_temp_set = temp_val_set;
                float temp = tc1_water_temp_set;
                if (temp < 0)
                    temp = 0;                        // 负数保护，和参考逻辑完全一致，避免异常负温乱码
                t_int = (int)temp;                   // 拆分整数部分
                t_frac = (int)((temp - t_int) * 10); // 仅取1位小数

                display_buf[0] = (t_int / 10) + '0'; // 整数部分：十位
                display_buf[1] = (t_int % 10) + '0'; // 整数部分：个位
                display_buf[2] = '.';                // 小数点
                display_buf[3] = t_frac + '0';       // 小数部分：仅保留1位

                // 显示TC1：
                OLED_ShowStr(80, 0, display_buf, 2);
            }
            else if (strcmp(modulestr, "TC2") == 0)
            {
                tc2_water_temp_set = temp_val_set;
                float temp = tc2_water_temp_set;
                if (temp < 0)
                    temp = 0;
                t_int = (int)temp;
                t_frac = (int)((temp - t_int) * 10);

                // 填充TC2的显示缓冲区
                display_buf[0] = (t_int / 10) + '0';
                display_buf[1] = (t_int % 10) + '0';
                display_buf[2] = '.';
                display_buf[3] = t_frac + '0';

                // 显示TC2：坐标为示例值，这里默认放在 第二行最左侧 (y=16对应12864 OLED的第二行，16x16字体的行高)
                // 如果你想让两个温度在同一行左右排列，可改为 x=40, y=0
                OLED_ShowStr(80, 2, display_buf, 2);
            }
        }
    }
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

// ==================== 3. 读取 TMP117 与 UI 极简刷新 ====================
// if (HAL_I2C_Mem_Read(&hi2c3, 0x90, 0x00, I2C_MEMADD_SIZE_8BIT, rx_data, 2, 100) == HAL_OK)
// {
//     raw_temp = (rx_data[0] << 8) | rx_data[1];
//     real_temp = raw_temp * 0.0078125f;

//     // --- 纯数学拆解刷新 OLED：仅显示体温 ---
//     if (real_temp < 0)
//         real_temp = 0;
//     t_int = (int)real_temp;
//     t_frac = (int)((real_temp - t_int) * 10);

//     display_buf[0] = (t_int / 10) + '0';
//     display_buf[1] = (t_int % 10) + '0';
//     display_buf[2] = '.';
//     display_buf[3] = t_frac + '0';

//     OLED_ShowStr(80, 0, display_buf, 2);
//  移除了 Tank1 和 Tank2 的动态显示代码

// --- 调温服热泵智能控制逻辑 (联动 M207 和 光耦风扇，完全保留！) ---
// if (real_temp > (target_hot + hysteresis))
// {
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET); // 开风扇
//     Send_M207_Cmd("TC1:TCADJUSTTEMP=15.0\r");             // 设定制冷
//     HAL_Delay(50);
//     Send_M207_Cmd("TC1:TCSW=1\r"); // 启动TEC
// }
// else if (real_temp < (target_cold - hysteresis))
// {
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET); // 开风扇
//     Send_M207_Cmd("TC1:TCADJUSTTEMP=45.0\r");             // 设定制热
//     HAL_Delay(50);
//     Send_M207_Cmd("TC1:TCSW=1\r"); // 启动TEC
// }
// else if (real_temp >= (target_cold - hysteresis) && real_temp <= (target_hot + hysteresis))
// {
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET); // 关风扇
//     Send_M207_Cmd("TC1:TCSW=0\r");                      // 停机待命
// }

// else
// {
//     // ========= 安全网：如果 TMP117 断线，立刻在极简UI上报警并停机 =========
//     OLED_ShowStr(48, 0, (unsigned char *)"ERR!    ", 2);

//     // 强制关风扇，关 TEC，切断输出防止失控 (安全功能完全保留！)
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
//     Send_M207_Cmd("TC1:TCSW=0\r");
// }

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
