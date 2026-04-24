#include "oled.h"
#include "i2c.h"
#include "codetab.h"

// 向OLED写命令
void WriteCmd(unsigned char I2C_Command)
{
    HAL_I2C_Mem_Write(&hi2c1, 0x78, 0x00, I2C_MEMADD_SIZE_8BIT, &I2C_Command, 1, 100);
}

// 向OLED写数据
void WriteData(unsigned char I2C_Data)
{
    HAL_I2C_Mem_Write(&hi2c1, 0x78, 0x40, I2C_MEMADD_SIZE_8BIT, &I2C_Data, 1, 100);
}

// 设置OLED显示位置
void OLED_SetPos(unsigned char x, unsigned char y) 
{ 
    WriteCmd(0xb0+y);
    WriteCmd(((x&0xf0)>>4)|0x10);
    WriteCmd((x&0x0f)|0x01);
}

// 全屏填充
void OLED_Fill(unsigned char Fill_Data)
{
    unsigned char m,n;
    for(m=0; m<8; m++) // 128x64屏幕有8个页
    {
        WriteCmd(0xb0+m);
        WriteCmd(0x00);
        WriteCmd(0x10);
        for(n=0; n<128; n++)
        {
            WriteData(Fill_Data);
        }
    }
}

// 清屏
void OLED_CLS(void)
{
    OLED_Fill(0x00);
}

// 开启显示
void OLED_ON(void)
{
    WriteCmd(0X8D);
    WriteCmd(0X14);
    WriteCmd(0XAF);
}

// 关闭显示
void OLED_OFF(void)
{
    WriteCmd(0X8D);
    WriteCmd(0X10);
    WriteCmd(0XAE);
}

// 显示字符串
void OLED_ShowStr(unsigned char x, unsigned char y, unsigned char ch[], unsigned char TextSize)
{
    unsigned char c = 0,i = 0,j = 0;
    switch(TextSize)
    {
        // 可选：5x7超小字体（如果需要可以打开，默认用下面的标准字体）
        // case 0:
        // {
        //     while(ch[j] != '\0')
        //     {
        //         c = ch[j] - 32;
        //         if(x>123){x=0;y++;}
        //         OLED_SetPos(x,y);
        //         for(i=0;i<5;i++) WriteData(F5x7[c][i]);
        //         x+=5;j++;
        //     }
        // }break;
        case 1: // 6x8小字体
        {
            while(ch[j] != '\0')
            {
                c = ch[j] - 32;
                if(x>126){x=0;y++;}
                OLED_SetPos(x,y);
                for(i=0;i<6;i++) WriteData(F6x8[c][i]);
                x+=6;j++;
            }
        }break;
        case 2: // 8x16标准字体（和中文高度对齐）
        {
            while(ch[j] != '\0')
            {
                c = ch[j] - 32;
                if(x>120){x=0;y++;}
                OLED_SetPos(x,y);
                for(i=0;i<8;i++) WriteData(F8X16[c*16+i]);
                OLED_SetPos(x,y+1);
                for(i=0;i<8;i++) WriteData(F8X16[c*16+i+8]);
                x+=8;j++;
            }
        }break;
    }
}

// 显示16x16中文
void OLED_ShowCN(unsigned char x, unsigned char y, unsigned char N)
{
    unsigned char wm = 0;
    OLED_SetPos(x, y);
    for(wm = 0; wm < 16; wm++)
    {
        WriteData(Chinese_Words[N][wm]);
    }
    OLED_SetPos(x, y + 1);
    for(wm = 0; wm < 16; wm++)
    {
        WriteData(Chinese_Words[N][wm + 16]);
    }
}

// 初始化OLED
void OLED_Init(void)
{
    HAL_Delay(100);  // 等待屏幕系统就绪
    
    WriteCmd(0xAE); //display off
    WriteCmd(0x20);	//Set Memory Addressing Mode	
    WriteCmd(0x10);	//Page Addressing Mode
    WriteCmd(0xb0);	//Set Page Start Address
    WriteCmd(0xc8);	//Set COM Output Scan Direction
    WriteCmd(0x00); //---set low column address
    WriteCmd(0x10); //---set high column address
    WriteCmd(0x40); //--set start line address
    WriteCmd(0x81); //--set contrast control register
    WriteCmd(0xff); //对比度 0x00~0xff
    WriteCmd(0xa1); //--set segment re-map 0 to 127
    WriteCmd(0xa6); //--set normal display
    WriteCmd(0xa8); //--set multiplex ratio
    WriteCmd(0x3F); //128x64屏幕对应63=0x3F
    WriteCmd(0xa4); //Output follows RAM content
    WriteCmd(0xd3); //-set display offset
    WriteCmd(0x00); //-not offset
    WriteCmd(0xd5); //--set display clock
    WriteCmd(0xf0); //--set divide ratio
    WriteCmd(0xd9); //--set pre-charge period
    WriteCmd(0x22); //
    WriteCmd(0xda); //--set com pins hardware configuration
    WriteCmd(0x12); //128x64屏幕对应配置0x12
    WriteCmd(0xdb); //--set vcomh
    WriteCmd(0x20); //0x20,0.77xVcc
    WriteCmd(0x8d); //--set DC-DC enable
    WriteCmd(0x14); //
    WriteCmd(0xaf); //--turn on oled panel
    
    OLED_CLS();     // 清屏
}