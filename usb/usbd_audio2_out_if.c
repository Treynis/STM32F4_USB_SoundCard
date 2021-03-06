/* Includes ------------------------------------------------------------------*/
#include "usbd_audio_core.h"
#include "usbd_audio_out_if.h"

#include "stm32f4_discovery_audio_codec.h"
#include "stm32f4_discovery_lis302dl.h"

#include <string.h>
#include <stdio.h>

extern int WavePlayerInit(uint32_t AudioFreq);

//make sure it is larger as I2S FIFO size
#ifdef I2S_24BIT
uint16_t sampleBuffer[((48*8) * 200) / 2];	//sample frequency (1 packet per ms) times format (bytes)
#else
uint16_t sampleBuffer[((48*4) * 300) / 2];	//sample frequency (1 packet per ms) times format (bytes)
#endif
int inCurIndex = 0;

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */


/** @defgroup usbd_audio_out_if 
  * @brief usbd out interface module
  * @{
  */ 

/** @defgroup usbd_audio_out_if_Private_TypesDefinitions
  * @{
  */ 
/**
  * @}
  */ 


/** @defgroup usbd_audio_out_if_Private_Defines
  * @{
  */ 
/**
  * @}
  */ 


/** @defgroup usbd_audio_out_if_Private_Macros
  * @{
  */ 
/**
  * @}
  */ 


/** @defgroup usbd_audio_out_if_Private_FunctionPrototypes
  * @{
  */
static uint8_t  Init         (uint32_t  AudioFreq, uint32_t Volume, uint32_t options);
static uint8_t  DeInit       (uint32_t options);
static uint8_t  AudioCmd     (uint8_t* pbuf, uint32_t size, uint8_t cmd);
static uint8_t  VolumeCtl    (uint8_t vol);
static uint8_t  MuteCtl      (uint8_t cmd);
static uint8_t  PeriodicTC   (uint8_t cmd);
static uint8_t  GetState     (void);

/**
  * @}
  */ 

/** @defgroup usbd_audio_out_if_Private_Variables
  * @{
  */ 
AUDIO_FOPS_TypeDef  AUDIO_OUT_fops = 
{
  Init,
  DeInit,
  AudioCmd,
  VolumeCtl,
  MuteCtl,
  PeriodicTC,
  GetState
};

/*static*/ uint8_t AudioState = AUDIO_STATE_INACTIVE;

/**
  * @}
  */ 

/** @defgroup usbd_audio_out_if_Private_Functions
  * @{
  */ 

/**
  * @brief  Init
  *         Initialize and configures all required resources for audio play function.
  * @param  AudioFreq: Startup audio frequency.
  * @param  Volume: Startup volume to be set.
  * @param  options: specific options passed to low layer function.
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  Init         (uint32_t AudioFreq, 
                              uint32_t Volume, 
                              uint32_t options)
{
  static uint32_t Initialized = 0;

  /* Check if the low layer has already been initialized */
  if (Initialized == 0)
  {
	WavePlayerInit(I2S_AudioFreq_48k);

    /* Set the Initialization flag to prevent reinitializing the interface again */
    Initialized = 1;
  }
  
  /* Update the Audio state machine */
  AudioState = AUDIO_STATE_ACTIVE;
    
  return AUDIO_OK;
}

/**
  * @brief  DeInit
  *         Free all resources used by low layer and stops audio-play function.
  * @param  options: options passed to low layer function.
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  DeInit       (uint32_t options)
{
  /* Update the Audio state machine */
  AudioState = AUDIO_STATE_INACTIVE;
  
  return AUDIO_OK;
}

#ifdef I2S_24BIT

static int convertTo24Bit(register uint16_t *outBuf, register uint8_t *inBuf, register int size)
{
	/*
	 * convert the 24bit samples into 2x 16bit FiFo words, for I2S DMA
	 */
	register int i,j;
	register uint16_t s, s2;

	j = 0;

	//__disable_irq();

	for (i = 0; i < size; i += 12)
	{
		s2  = *inBuf++ << 0;
		s   = *inBuf++ << 0;
		s  |= *inBuf++ << 8;

		*outBuf++ = s;
		*outBuf++ = s2;

		s2  = *inBuf++ << 0;
		s   = *inBuf++ << 0;
		s  |= *inBuf++ << 8;

		*outBuf++ = s;
		*outBuf++ = s2;

		//roll out the loop a bit

		s2  = *inBuf++ << 0;
		s   = *inBuf++ << 0;
		s  |= *inBuf++ << 8;

		*outBuf++ = s;
		*outBuf++ = s2;

		s2  = *inBuf++ << 0;
		s   = *inBuf++ << 0;
		s  |= *inBuf++ << 8;

		*outBuf++ = s;
		*outBuf++ = s2;

		j += 8;
	}

	//__enable_irq();

	return j;
}
#endif

/**
  * @brief  AudioCmd 
  *         Play, Stop, Pause or Resume current file.
  * @param  pbuf: address from which file should be played.
  * @param  size: size of the current buffer/file.
  * @param  cmd: command to be executed, can be AUDIO_CMD_PLAY , AUDIO_CMD_PAUSE, 
  *              AUDIO_CMD_RESUME or AUDIO_CMD_STOP.
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  AudioCmd(uint8_t* pbuf, 
                         uint32_t size,
                         uint8_t cmd)
{
  static int startPlay = 0;
  //printf("audio cmd: %x, %x\n", cmd, (int)AudioState);

  /* Check the current state */
  if ((AudioState == AUDIO_STATE_INACTIVE) || (AudioState == AUDIO_STATE_ERROR))
  {
    AudioState = AUDIO_STATE_ERROR;
    return AUDIO_FAIL;
  }
  
  switch (cmd)
  {
    /* Process the PLAY command ----------------------------*/
  case AUDIO_CMD_PLAY:
    /* If current state is Active or Stopped */
    if ((AudioState == AUDIO_STATE_ACTIVE) || \
       (AudioState == AUDIO_STATE_STOPPED) || \
       (AudioState == AUDIO_STATE_PLAYING) || \
       (AudioState == AUDIO_STATE_PAUSED))
    {
      if (AudioState != AUDIO_STATE_PLAYING) {
//        EVAL_AUDIO_PauseResume(AUDIO_RESUME);
      }
      AudioState = AUDIO_STATE_PLAYING;

      if (inCurIndex < (sizeof(sampleBuffer) / 2))
      {
#ifdef I2S_24BIT
    	  inCurIndex += convertTo24Bit(&sampleBuffer[inCurIndex], pbuf, size);
#else
    	  memcpy(&sampleBuffer[inCurIndex], pbuf, size);
    	  inCurIndex += size / 2;
#endif
      }
      else
      {

#ifdef I2S_24BIT
    	  //for 24bit we have to reorder, 16bit Little Endian words assumed for I2S DMA
    	  //word  0x8EAA33 must become 0x33008EAA or as bytes:
    	  //bytes 0x8E 0xAA 0x33 -> 0xAA 0x8E 0x00 0x33
    	  inCurIndex = convertTo24Bit(sampleBuffer, pbuf, size);
#else
    	  memcpy(&sampleBuffer[0], pbuf, size);
    	  inCurIndex = size / 2;
#endif
      }

      if ( ! startPlay)
      {
    	  //make sure to have enough data so that DMA can fill I2S FIFO completely, here half buffer filled
    	  if (inCurIndex >= (sizeof(sampleBuffer) / 4))
    	  {
    		  EVAL_AUDIO_Play(sampleBuffer, sizeof(sampleBuffer));

//              inCurIndex = 0;
    		  startPlay = 1;
    	  }
      }

      return AUDIO_OK;
    }
    else /* Not allowed command */
    {
      return AUDIO_FAIL;
    }
    
    /* Process the STOP command ----------------------------*/
    /* Process the PAUSE command ---------------------------*/
  case AUDIO_CMD_STOP:
  case AUDIO_CMD_PAUSE:
    if (AudioState != AUDIO_STATE_PLAYING)
    {
      /* Unsupported command */
      return AUDIO_FAIL;
    } else {
        if (AudioState == AUDIO_STATE_PLAYING)
        {
            EVAL_AUDIO_PauseResume(AUDIO_PAUSE);
            memset(sampleBuffer, 0, sizeof(sampleBuffer));
            inCurIndex = 0;

        }
        AudioState = AUDIO_STATE_PAUSED;
        return AUDIO_OK;
    }
    
    /* Unsupported command ---------------------------------*/
  default:
    return AUDIO_FAIL;
  }  
}

/**
  * @brief  VolumeCtl
  *         Set the volume level in %
  * @param  vol: volume level to be set in % (from 0% to 100%)
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  VolumeCtl    (uint8_t vol)
{
  //UART_putString("VolumeCtl\n");
  /* Call low layer volume setting function */  
  if (EVAL_AUDIO_VolumeCtl(vol) != 0)
  {
    AudioState = AUDIO_STATE_ERROR;
    return AUDIO_FAIL;
  }
  
  return AUDIO_OK;
}

/**
  * @brief  MuteCtl
  *         Mute or Unmute the audio current output
  * @param  cmd: can be 0 to unmute, or 1 to mute.
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  MuteCtl      (uint8_t cmd)
{
	//UART_putString("MuteCtl\n");

  /* Call low layer mute setting function */  
  if (EVAL_AUDIO_Mute(cmd) != 0)
  {
    AudioState = AUDIO_STATE_ERROR;
    return AUDIO_FAIL;
  }
  
  return AUDIO_OK;
}

/**
  * @brief  
  *         
  * @param  
  * @param  
  * @retval AUDIO_OK if all operations succeed, AUDIO_FAIL else.
  */
static uint8_t  PeriodicTC   (uint8_t cmd)
{
  return AUDIO_OK;
}


/**
  * @brief  GetState
  *         Return the current state of the audio machine
  * @param  None
  * @retval Current State.
  */
static uint8_t  GetState   (void)
{
  return AudioState;
}

/**
  * @}
  */ 

/**
  * @}
  */ 

/**
  * @}
  */ 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
