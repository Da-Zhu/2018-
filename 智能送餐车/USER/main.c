#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "includes.h"
#include "key.h"
#include "lcd.h"
#include "ltdc.h" 
#include "sdram.h"
#include "w25qxx.h"
#include "nand.h"  
#include "mpu.h"
#include "sdmmc_sdcard.h"
#include "malloc.h"
#include "ff.h"
#include "exfuns.h"
#include "fontupd.h"
#include "text.h"
#include "ov5640.h" 
#include "dcmi.h"  
#include "pcf8574.h" 
#include "atk_qrdecode.h"



/************************************************
 ALIENTEK ������STM32F7������ UCOSIIIʵ��
 ��4-1 UCOSII��ֲʵ��
 
 UCOSIII���������ȼ��û�������ʹ�ã�ALIENTEK
 ����Щ���ȼ��������UCOSIII��5��ϵͳ�ڲ�����
 ���ȼ�0���жϷ������������� OS_IntQTask()
 ���ȼ�1��ʱ�ӽ������� OS_TickTask()
 ���ȼ�2����ʱ���� OS_TmrTask()
 ���ȼ�OS_CFG_PRIO_MAX-2��ͳ������ OS_StatTask()
 ���ȼ�OS_CFG_PRIO_MAX-1���������� OS_IdleTask()
 ����֧�֣�www.openedv.com
 �Ա����̣�http://eboard.taobao.com 
 ��ע΢�Ź���ƽ̨΢�źţ�"����ԭ��"����ѻ�ȡSTM32���ϡ�
 ������������ӿƼ����޹�˾  
 ���ߣ�����ԭ�� @ALIENTEK
************************************************/

//�������ȼ�
#define START_TASK_PRIO		3
//�����ջ��С	
#define START_STK_SIZE 		512
//������ƿ�
OS_TCB StartTaskTCB;
//�����ջ	
CPU_STK START_TASK_STK[START_STK_SIZE];
//������
void start_task(void *p_arg);

//�������ȼ�
#define LED0_TASK_PRIO		4
//�����ջ��С	
#define LED0_STK_SIZE 		512
//������ƿ�
OS_TCB Led0TaskTCB;
//�����ջ	
CPU_STK LED0_TASK_STK[LED0_STK_SIZE];
void led0_task(void *p_arg);

//�������ȼ�
#define LED1_TASK_PRIO		5
//�����ջ��С	
#define LED1_STK_SIZE       128
//������ƿ�
OS_TCB Led1TaskTCB;
//�����ջ	
CPU_STK LED1_TASK_STK[LED1_STK_SIZE];
//������
void led1_task(void *p_arg);

//�������ȼ�
#define FLOAT_TASK_PRIO		6
//�����ջ��С
#define FLOAT_STK_SIZE		256
//������ƿ�
OS_TCB	FloatTaskTCB;
//�����ջ
__align(8) CPU_STK	FLOAT_TASK_STK[FLOAT_STK_SIZE];
//������
void float_task(void *p_arg);



u16 qr_image_width;						//����ʶ��ͼ��Ŀ�ȣ�����=��ȣ�
u8 	readok=0;									//�ɼ���һ֡���ݱ�ʶ
u32 *dcmi_line_buf[2];				//����ͷ����һ��һ�ж�ȡ,�����л���  
u16 *rgb_data_buf;						//RGB565֡����buf 
u16 dcmi_curline=0;						//����ͷ�������,��ǰ�б��	



//����ͷ����DMA��������жϻص�����
void qr_dcmi_rx_callback(void)
{  
	OS_ERR err;
	CPU_SR_ALLOC();
	u32 *pbuf;
	u16 i;
	pbuf=(u32*)(rgb_data_buf+dcmi_curline*qr_image_width);//��rgb_data_buf��ַƫ�Ƹ�ֵ��pbuf
	
	if(DMADMCI_Handler.Instance->CR&(1<<19))//DMAʹ��buf1,��ȡbuf0
	{ 
		for(i=0;i<qr_image_width/2;i++)
		{
			pbuf[i]=dcmi_line_buf[0][i];
		} 
	}else 										//DMAʹ��buf0,��ȡbuf1
	{
		for(i=0;i<qr_image_width/2;i++)
		{
			pbuf[i]=dcmi_line_buf[1][i];
		} 
	} 
	dcmi_curline++;
}

//��ʾͼ��
void qr_show_image(u16 xoff,u16 yoff,u16 width,u16 height,u16 *imagebuf)
{
	OS_ERR err;
	CPU_SR_ALLOC();
	u16 linecnt=yoff;
	
	if(lcdltdc.pwidth!=0)//RGB��
	{
		for(linecnt=0;linecnt<height;linecnt++)
		{
			LTDC_Color_Fill(xoff,linecnt+yoff,xoff+width-1,linecnt+yoff,imagebuf+linecnt*width);//RGB��,DM2D��� 
		}
		
	}else LCD_Color_Fill(xoff,yoff,xoff+width-1,yoff+height-1,imagebuf);	//MCU��,ֱ����ʾ
}

//imagewidth:<=240;����240ʱ,��240��������
//imagebuf:RGBͼ�����ݻ�����
void qr_decode(u16 imagewidth,u16 *imagebuf)
{
	OS_ERR err;
	CPU_SR_ALLOC();
	static u8 bartype=0; 
	u8 *bmp;
	u8 *result=NULL;
	u16 Color;
	u16 i,j;	
	u16 qr_img_width=0;						//����ʶ������ͼ����,��󲻳���240!
	u8 qr_img_scale=0;						//ѹ����������
	
	if(imagewidth>240)
	{
		if(imagewidth%240)return ;	//����240�ı���,ֱ���˳�
		qr_img_width=240;
		qr_img_scale=imagewidth/qr_img_width;
	}else
	{
		qr_img_width=imagewidth;
		qr_img_scale=1;
	}  
	result=mymalloc(SRAMIN,1536);//����ʶ��������ڴ�
	bmp=mymalloc(SRAMDTCM,qr_img_width*qr_img_width);//DTCM�����ڴ�Ϊ120K����������240*240=56K 
	mymemset(bmp,0,qr_img_width*qr_img_width);
	if(lcdltdc.pwidth==0)//MCU��,���辵��
	{ 
		for(i=0;i<qr_img_width;i++)		
		{
			for(j=0;j<qr_img_width;j++)		//��RGB565ͼƬת�ɻҶ�
			{	
				Color=*(imagebuf+((i*imagewidth)+j)*qr_img_scale); //����qr_img_scaleѹ����240*240
				*(bmp+i*qr_img_width+j)=(((Color&0xF800)>> 8)*76+((Color&0x7E0)>>3)*150+((Color&0x001F)<<3)*30)>>8;
			}		
		}
	}else	//RGB��,��Ҫ����
	{
		for(i=0;i<qr_img_width;i++)		
		{
			for(j=0;j<qr_img_width;j++)		//��RGB565ͼƬת�ɻҶ�
			{	
				Color=*(imagebuf+((i*imagewidth)+qr_img_width-j-1)*qr_img_scale);//����qr_img_scaleѹ����240*240
				*(bmp+i*qr_img_width+j)=(((Color&0xF800)>> 8)*76+((Color&0x7E0)>>3)*150+((Color&0x001F)<<3)*30)>>8;
			}		
		}		
	}
	atk_qr_decode(qr_img_width,qr_img_width,bmp,bartype,result);//ʶ��Ҷ�ͼƬ��ע�⣺���κ�ʱԼ0.2S��
	
	if(result[0]==0)//û��ʶ�����
	{
		bartype++;
		if(bartype>=5)bartype=0; 
	}
	else if(result[0]!=0)//ʶ������ˣ���ʾ���
	{	
		PCF8574_WriteBit(BEEP_IO,0);//�򿪷�����
		OSTimeDlyHMSM(0,0,0,100,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms
		PCF8574_WriteBit(BEEP_IO,1);
		POINT_COLOR=BLUE; 
		LCD_Fill(0,(lcddev.height+qr_image_width)/2+20,lcddev.width,lcddev.height,BLACK);
		Show_Str(0,(lcddev.height+qr_image_width)/2+20,lcddev.width,
								(lcddev.height-qr_image_width)/2-20,(u8*)result,16,0							
						);//LCD��ʾʶ����
		printf("\r\nresult:\r\n%s\r\n",result);//���ڴ�ӡʶ���� 		
	}
	myfree(SRAMDTCM,bmp);		//�ͷŻҶ�ͼbmp�ڴ�
	myfree(SRAMIN,result);	//�ͷ�ʶ����	
}  



int main(void)
{
    OS_ERR err;
	CPU_SR_ALLOC();
    
	float fac;
	
    Write_Through();                //͸д
    Cache_Enable();                 //��L1-Cache
    HAL_Init();				        //��ʼ��HAL��
    Stm32_Clock_Init(432,25,2,9);   //����ʱ��,216Mhz 
    delay_init(216);                //��ʱ��ʼ��
	uart_init(115200);		        //���ڳ�ʼ��
    LED_Init();                     //��ʼ��LED
	KEY_Init();                     //��ʼ������
	SDRAM_Init();                   //��ʼ��SDRAM
	LCD_Init();                     //��ʼ��LCD
	W25QXX_Init();				   				//��ʼ��W25Q256
	PCF8574_Init();									//��ʼ��PCF8574
	OV5640_Init();									//��ʼ��OV5640
	my_mem_init(SRAMIN);            //��ʼ���ڲ��ڴ��
	my_mem_init(SRAMEX);            //��ʼ���ⲿSDRAM�ڴ��
	my_mem_init(SRAMDTCM);          //��ʼ���ڲ�DTCM�ڴ��
	POINT_COLOR=RED; 
	LCD_Clear(BLACK); 	
	while(font_init()) 		//����ֿ�
	{	    
		LCD_ShowString(30,50,200,16,16,(u8*)"Font Error!");
		OSTimeDlyHMSM(0,0,0,200,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms				  
		LCD_Fill(30,50,240,66,WHITE);//�����ʾ	     
		OSTimeDlyHMSM(0,0,0,200,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms				  
	}  	 
 	Show_Str_Mid(0,0,(u8*)"������STM32F4/F7������",16,lcddev.width);	 			    	 
	Show_Str_Mid(0,20,(u8*)"��ά��/������ʶ��ʵ��",16,lcddev.width);	
	while(OV5640_Init())//��ʼ��OV5640
	{
		Show_Str(30,190,240,16,(u8*)"OV5640 ����!",16,0);
		OSTimeDlyHMSM(0,0,0,200,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms
	    LCD_Fill(30,190,239,206,WHITE);
		OSTimeDlyHMSM(0,0,0,200,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms
	}	
	//�Զ��Խ���ʼ��
	OV5640_RGB565_Mode();		//RGB565ģʽ 
	OV5640_Focus_Init(); 
	OV5640_Light_Mode(0);		//�Զ�ģʽ
	OV5640_Color_Saturation(3);	//ɫ�ʱ��Ͷ�0
	OV5640_Brightness(4);		//����0
	OV5640_Contrast(3);			//�Աȶ�0
	OV5640_Sharpness(33);		//�Զ����
	OV5640_Focus_Constant();//���������Խ�
	DCMI_Init();						//DCMI���� 

		
	qr_image_width=lcddev.width;
	if(qr_image_width>480)qr_image_width=480;//����qr_image_width����Ϊ240�ı���
	if(qr_image_width==320)qr_image_width=240;
	Show_Str(0,(lcddev.height+qr_image_width)/2+4,240,16,(u8*)"ʶ������",16,1);
	
	dcmi_line_buf[0]=mymalloc(SRAMIN,qr_image_width*2);						//Ϊ�л�����������ڴ�	
	dcmi_line_buf[1]=mymalloc(SRAMIN,qr_image_width*2);						//Ϊ�л�����������ڴ�
	rgb_data_buf=mymalloc(SRAMEX,qr_image_width*qr_image_width*2);//Ϊrgb֡���������ڴ�
	
	dcmi_rx_callback=qr_dcmi_rx_callback;//DMA���ݽ����жϻص�����
	DCMI_DMA_Init((u32)dcmi_line_buf[0],(u32)dcmi_line_buf[1],qr_image_width/2,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);//DCMI DMA����  
	fac=800/qr_image_width;	//�õ���������
	OV5640_OutSize_Set((1280-fac*qr_image_width)/2,(800-fac*qr_image_width)/2,qr_image_width,qr_image_width); 
	DCMI_Start(); 					//��������	 
		
	printf("SRAM IN:%d\r\n",my_mem_perused(SRAMIN));
	printf("SRAM EX:%d\r\n",my_mem_perused(SRAMEX));
	printf("SRAM DCTM:%d\r\n",my_mem_perused(SRAMDTCM)); 
	
	atk_qr_init();//��ʼ��ʶ��⣬Ϊ�㷨�����ڴ�
	
	printf("1SRAM IN:%d\r\n",my_mem_perused(SRAMIN));
	printf("1SRAM EX:%d\r\n",my_mem_perused(SRAMEX));
	printf("1SRAM DCTM:%d\r\n",my_mem_perused(SRAMDTCM));
	
	

	OSInit(&err);		            //��ʼ��UCOSIII
	OS_CRITICAL_ENTER();            //�����ٽ���
	//������ʼ����
	OSTaskCreate((OS_TCB 	* )&StartTaskTCB,		//������ƿ�
				 (CPU_CHAR	* )"start task", 		//��������
                 (OS_TASK_PTR )start_task, 			//������
                 (void		* )0,					//���ݸ��������Ĳ���
                 (OS_PRIO	  )START_TASK_PRIO,     //�������ȼ�
                 (CPU_STK   * )&START_TASK_STK[0],	//�����ջ����ַ
                 (CPU_STK_SIZE)START_STK_SIZE/10,	//�����ջ�����λ
                 (CPU_STK_SIZE)START_STK_SIZE,		//�����ջ��С
                 (OS_MSG_QTY  )0,					//�����ڲ���Ϣ�����ܹ����յ������Ϣ��Ŀ,Ϊ0ʱ��ֹ������Ϣ
                 (OS_TICK	  )0,					//��ʹ��ʱ��Ƭ��תʱ��ʱ��Ƭ���ȣ�Ϊ0ʱΪĬ�ϳ��ȣ�
                 (void   	* )0,					//�û�����Ĵ洢��
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR|OS_OPT_TASK_SAVE_FP, //����ѡ��,Ϊ�˱���������������񶼱��渡��Ĵ�����ֵ
                 (OS_ERR 	* )&err);				//��Ÿú�������ʱ�ķ���ֵ
	OS_CRITICAL_EXIT();	//�˳��ٽ���	 
	OSStart(&err);      //����UCOSIII
    while(1)
    {
	} 
}

//��ʼ������
void start_task(void *p_arg)
{
	OS_ERR err;
	CPU_SR_ALLOC();
	p_arg = p_arg;

	CPU_Init();
#if OS_CFG_STAT_TASK_EN > 0u
   OSStatTaskCPUUsageInit(&err);  	//ͳ������                
#endif
	
#ifdef CPU_CFG_INT_DIS_MEAS_EN		//���ʹ���˲����жϹر�ʱ��
    CPU_IntDisMeasMaxCurReset();	
#endif

#if	OS_CFG_SCHED_ROUND_ROBIN_EN  //��ʹ��ʱ��Ƭ��ת��ʱ��
	 //ʹ��ʱ��Ƭ��ת���ȹ���,����Ĭ�ϵ�ʱ��Ƭ����s
	OSSchedRoundRobinCfg(DEF_ENABLED,10,&err);  
#endif		
	
	OS_CRITICAL_ENTER();	//�����ٽ���
	//����LED0����
	OSTaskCreate((OS_TCB 	* )&Led0TaskTCB,		
				 (CPU_CHAR	* )"led0 task", 		
                 (OS_TASK_PTR )led0_task, 			
                 (void		* )0,					
                 (OS_PRIO	  )LED0_TASK_PRIO,     
                 (CPU_STK   * )&LED0_TASK_STK[0],	
                 (CPU_STK_SIZE)LED0_STK_SIZE/10,	
                 (CPU_STK_SIZE)LED0_STK_SIZE,		
                 (OS_MSG_QTY  )0,					
                 (OS_TICK	  )0,					
                 (void   	* )0,					
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR|OS_OPT_TASK_SAVE_FP,
                 (OS_ERR 	* )&err);				
				 
	//����LED1����
	OSTaskCreate((OS_TCB 	* )&Led1TaskTCB,		
				 (CPU_CHAR	* )"led1 task", 		
                 (OS_TASK_PTR )led1_task, 			
                 (void		* )0,					
                 (OS_PRIO	  )LED1_TASK_PRIO,     	
                 (CPU_STK   * )&LED1_TASK_STK[0],	
                 (CPU_STK_SIZE)LED1_STK_SIZE/10,	
                 (CPU_STK_SIZE)LED1_STK_SIZE,		
                 (OS_MSG_QTY  )0,					
                 (OS_TICK	  )0,					
                 (void   	* )0,				
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR|OS_OPT_TASK_SAVE_FP, 
                 (OS_ERR 	* )&err);
				 
	//���������������
	OSTaskCreate((OS_TCB 	* )&FloatTaskTCB,		
				 (CPU_CHAR	* )"float test task", 		
                 (OS_TASK_PTR )float_task, 			
                 (void		* )0,					
                 (OS_PRIO	  )FLOAT_TASK_PRIO,     	
                 (CPU_STK   * )&FLOAT_TASK_STK[0],	
                 (CPU_STK_SIZE)FLOAT_STK_SIZE/10,	
                 (CPU_STK_SIZE)FLOAT_STK_SIZE,		
                 (OS_MSG_QTY  )0,					
                 (OS_TICK	  )0,					
                 (void   	* )0,				
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR|OS_OPT_TASK_SAVE_FP, 
                 (OS_ERR 	* )&err);
	OS_CRITICAL_EXIT();	//�����ٽ���				 
	OS_TaskSuspend((OS_TCB*)&StartTaskTCB,&err);		//����ʼ����			 
}

//led0������
void led0_task(void *p_arg)
{
							 
 	u8 key;						   
	u8 i;
	
	OS_ERR err;
	p_arg = p_arg;
	

//		while(1)
//		{
//			key=KEY_Scan(0);//��֧������
//			if(key)
//			{ 
//				OV5640_Focus_Single();  //��KEY0��KEY1��KEYUP�ֶ������Զ��Խ�
//				
//				if(key==KEY2_PRES)break;//��KEY2����ʶ��
//			} 
//			if(readok==1)			//�ɼ�����һ֡ͼ��
//			{		
//				readok=0;
//				qr_show_image((lcddev.width-qr_image_width)/2,(lcddev.height-qr_image_width)/2,qr_image_width,qr_image_width,rgb_data_buf);
//				qr_decode(qr_image_width,rgb_data_buf);
//			}
//			i++;
//			if(i==20)//DS0��˸.
//			{
//				i=0;
//				LED0_Toggle;
//			}
////			OSTimeDlyHMSM(0,0,0,20,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms
//		}
//		atk_qr_destroy();//�ͷ��㷨�ڴ�
//		printf("3SRAM IN:%d\r\n",my_mem_perused(SRAMIN));
//		printf("3SRAM EX:%d\r\n",my_mem_perused(SRAMEX));
//		printf("3SRAM DCTM:%d\r\n",my_mem_perused(SRAMDTCM)); 
		while(1)
		{
			LED0_Toggle;
			OSTimeDlyHMSM(0,0,0,200,OS_OPT_TIME_HMSM_STRICT,&err); //��ʱ200ms
			if(readok==1)			//�ɼ�����һ֡ͼ��
			{		
				readok=0;
				qr_show_image((lcddev.width-qr_image_width)/2,(lcddev.height-qr_image_width)/2,qr_image_width,qr_image_width,rgb_data_buf);
				qr_decode(qr_image_width,rgb_data_buf);
			}
		}

}

//led1������
void led1_task(void *p_arg)
{
	p_arg = p_arg;
	while(1)
	{
		printf("aaaa\r\n");
        delay_ms(500);//��ʱ500ms
	}
}

//�����������
void float_task(void *p_arg)
{
	CPU_SR_ALLOC();
	static double double_num=0.00;
	while(1)
	{
		double_num+=0.01f;
		OS_CRITICAL_ENTER();	//�����ٽ���
		printf("double_num��ֵΪ: %.4f\r\n",double_num);
		OS_CRITICAL_EXIT();		//�˳��ٽ���
		delay_ms(1000);			//��ʱ1000ms
	}
}

