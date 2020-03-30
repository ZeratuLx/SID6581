//MIT License
//
//Copyright (c) 2020 Yves BAZIN
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#include "SID6581.h"
#include "FS.h"
#include "SPI.h"
#include "Arduino.h"
#include "driver/i2s.h"
#include "freertos/queue.h"



SID6581::SID6581(){}

 bool SID6581::begin(int clock_pin,int data_pin, int latch,int sid_clock_pin)
{
    const i2s_port_t i2s_num = ( i2s_port_t)0;
    const i2s_config_t i2s_config = {
        .mode =(i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 31250,
        .bits_per_sample = (i2s_bits_per_sample_t)16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t) (I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0, // default interrupt priority
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };
    const i2s_pin_config_t pin_config = {
        .bck_io_num = sid_clock_pin,
        .ws_io_num = NULL,
        .data_out_num = NULL,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);   //install and start i2s driver
    
    i2s_set_pin(i2s_num, &pin_config);
    begin(clock_pin,data_pin,latch );
    
    
}
bool SID6581::begin(int clock_pin,int data_pin, int latch )
{
    /*
     We set up the spi bus which connects to the 74HC595
     */
    sid_spi = new SPIClass(HSPI);
    if(sid_spi==NULL)
        return false;
    sid_spi->begin(clock_pin,NULL,data_pin,NULL);
    latch_pin=latch;
    pinMode(latch, OUTPUT);
    
    Serial.println("SID Initialized");
    _sid_queue = xQueueCreate( SID_QUEUE_SIZE, sizeof( _sid_register_to_send ) );
    xTaskCreate(SID6581::_pushRegister, "_pushRegister", 2048, this,3, &xPushToRegisterHandle);
    
    // sid_spi->beginTransaction(SPISettings(sid_spiClk, LSBFIRST, SPI_MODE0));
    resetsid();
    volume=15;
    numberOfSongs=0;
    currentSong=0;
    
}

void SID6581::sidSetMaxVolume( uint8_t vol)
{
    volume=vol;
}




void SID6581::addSong(fs::FS &fs,  const char * path)
{
    songstruct p1;
    p1.fs=(fs::FS *)&fs;
    //char h[250];
    sprintf(p1.filename,"%s",path);
    //p1.filename=h;
    listsongs[numberOfSongs]=p1;
    numberOfSongs++;
    Serial.printf("nb song:%d\n",numberOfSongs);
}


void SID6581::pausePlay()
{
    
    if(xPlayerTaskHandle!=NULL)
    {
        
        if(!paused)
        {
            
            paused=true;
        }
        else
        {
            soundOn();
            
            paused=false;
            if(PausePlayTaskLocker!=NULL)
                xTaskNotifyGive(PausePlayTaskLocker);
        }
    }
}

void SID6581::playSIDTunesLoopTask(void *pvParameters)
{
    SID6581 * sid= (SID6581 *)pvParameters;
    
    
    while(1 && !sid->stopped)
    {
        if(SIDPlayerTaskHandle  == NULL)
        {
            sid->playNextInt();
            SIDPlayerTaskHandle  = xTaskGetCurrentTaskHandle();
            //yield();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            SIDPlayerTaskHandle  = NULL;
        }
    }
    SIDPlayerLoopTaskHandle=NULL;
    SIDPlayerTaskHandle=NULL;
    vTaskDelete(SIDPlayerLoopTaskHandle);
}

void SID6581::play()
{
    if(SIDPlayerLoopTaskHandle!=NULL)
    {
        Serial.println("allready Playing");
        return;
    }
    stopped=false;
    paused=false;
    currentSong=numberOfSongs-1;
    xTaskCreate(SID6581::playSIDTunesLoopTask, "playSIDTunesLoopTask", 4096, this,1, &SIDPlayerLoopTaskHandle);
}

void SID6581::stopPlayer()
{
    stop();
    stopped=true;
    if(SIDPlayerTaskHandle!=NULL)
        xTaskNotifyGive(SIDPlayerTaskHandle);
    SIDPlayerLoopTaskHandle=NULL;
    SIDPlayerTaskHandle=NULL;
}

void SID6581::stop()
{
    if(xPlayerTaskHandle!=NULL)
    {
        //we stop the current song
        //we unlock the pause locker in case of
        if(PausePlayTaskLocker!=NULL)
            xTaskNotifyGive(PausePlayTaskLocker);
        soundOff();
        resetsid();
        free(sidvalues);
        vTaskDelete(xPlayerTaskHandle);
        xPlayerTaskHandle=NULL;
        //sid_spi->endTransaction();
    }
    
}



void SID6581::playNext()
{
    stop();
    if(SIDPlayerTaskHandle!=NULL)
        xTaskNotifyGive(SIDPlayerTaskHandle);
    
}

void SID6581::playPrev()
{
    stop();
    if(SIDPlayerTaskHandle!=NULL)
    {
        if(currentSong==0)
            currentSong=numberOfSongs-2;
        else
            currentSong=currentSong-2;
        xTaskNotifyGive(SIDPlayerTaskHandle);
    }
    
    
}


void SID6581::playNextInt()
{
    stop();
    
    currentSong=(currentSong+1)%numberOfSongs;
    playSongNumber(currentSong);
    
}

void SID6581::playSongNumber(int number)
{
    if(xPlayerTaskHandle!=NULL)
    {
        Serial.println("allready Playing");
        return;
    }
    if(number>_maxnumber)
    {
        Serial.println("Id too long");
        return;
    }

    Serial.println("playing song");
    
    paused=false;
    numberToPlay=number;
    xTaskCreate(SID6581::playSIDTunesTask, "playSIDTunesTask", 4096, this,1, &xPlayerTaskHandle);
    
}

void SID6581::setVoice(uint8_t vo)
{
    voice=vo;
}
void SID6581::playSIDTunesTask(void *pvParameters)
{
    SID6581 * sid= (SID6581 *)pvParameters;
    sid->resetsid();
    songstruct p;
    p=sid->listsongs[sid->numberToPlay];
    unsigned int sizet=sid->readFile2(*p.fs,p.filename);
    if(sizet==0)
    {
        Serial.println("error reading the file");
        if(sid->sidvalues==NULL)
            free(sid->sidvalues);
        xPlayerTaskHandle=NULL;
        if(SIDPlayerTaskHandle!=NULL)
            xTaskNotifyGive(SIDPlayerTaskHandle);
        vTaskDelete(NULL);
        
    }
    
    Serial.printf("palying:%s\n",p.filename);
    uint8_t *d=sid->sidvalues;
    uint32_t counttime=0;
    uint32_t counttime1=0;
    
    
    for (uint32_t i=0;i<sizet;i++)
    {
        
       
        //Serial.printf("%d %d %ld")
//        if((*(uint8_t*)d)%24==0) //we delaonf with the sound
//        {
//            sid->save24=*(uint8_t*)(d+1);
//            uint8_t value=*(uint8_t*)(d+1);
//            value=value& 0xf0 +( ((value& 0x0f)*sid->volume)/15)  ;
//            *(uint8_t*)(d+1)=value;
//        }
        
        
         sid->pushRegister((*(uint8_t*)d)/32,(*(uint8_t*)d)%32,*(uint8_t*)(d+1));

        if(*(uint16_t*)(d+2)>4)
            delayMicroseconds(*(uint16_t*)(d+2)-4);
        else
            delayMicroseconds(*(uint16_t*)(d+2));
        d+=4;
        sid->feedTheDog();
        if(sid->paused)
        {
            sid->soundOff();
            PausePlayTaskLocker  = xTaskGetCurrentTaskHandle();
            //yield();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            PausePlayTaskLocker=NULL;
        }
    }
    //sid->sid_spi->endTransaction();
    sid->resetsid();
    free(sid->sidvalues);
    Serial.println("end Play");
    xPlayerTaskHandle=NULL;
    if(SIDPlayerTaskHandle!=NULL)
        xTaskNotifyGive(SIDPlayerTaskHandle);
    vTaskDelete(NULL);
    
}
void SID6581::feedTheDog(){
    // feed dog 0
    TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE; // write enable
    TIMERG0.wdt_feed=1;                       // feed dog
    TIMERG0.wdt_wprotect=0;                   // write protect
    // feed dog 1
    TIMERG1.wdt_wprotect=TIMG_WDT_WKEY_VALUE; // write enable
    TIMERG1.wdt_feed=1;                       // feed dog
    TIMERG1.wdt_wprotect=0;                   // write protect
}

void SID6581::soundOff()
{
    setcsw();
    setA(24);
    setD(save24 &0xf0);
    clearcsw(0);
    
}



void SID6581::setFrequencyHz(int voice,double frequencyHz)
{
    //we do calculate the fréquency used by the 6581
    //Fout = (Fn * Fclk/16777216) Hz
    //Fn=Fout*16777216/Fclk here Fclk is the speed of you clock 1Mhz
    double fout=frequencyHz*16.777216;
    setFrequency(voice,round(fout));
}

double SID6581::getFrequencyHz(int voice)
{
    return (double)getFrequency(voice)/16.777216;
}


void SID6581::setFrequency(int voice, uint16_t frequency)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].frequency=frequency;
    voices[voice].freq_lo=frequency & 0xff;
    voices[voice].freq_hi=(frequency >> 8) & 0xff;
    // Serial.println(voices[voice].freq_hi);
    pushToVoice(voice,0,voices[voice].freq_lo);
    pushToVoice(voice,1,voices[voice].freq_hi);
}

int SID6581::getFrequency(int voice)
{
    int chip=voice/3;
    return sidregisters[chip*32+voice*7]+sidregisters[chip*32+voice*7+1]*256;
}

void SID6581::setPulse(int voice, uint16_t pulse)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].pulse=pulse;
    voices[voice].pw_lo=pulse & 0xff;
    voices[voice].pw_hi=(pulse >> 8) & 0x0f;
    pushToVoice(voice,2,voices[voice].pw_lo);
    pushToVoice(voice,3,voices[voice].pw_hi);
}

int SID6581::getPulse(int voice)
{
    int chip=voice/3;
    return sidregisters[chip*32+voice*7+2]+sidregisters[chip*32+voice*7+3]*256;

}

void SID6581::setEnv(int voice, uint8_t att,uint8_t decay,uint8_t sutain, uint8_t release)
{
//    if(voice <0 or voice >2)
//        return;
    setAttack(voice,  att);
    setDecay(voice,  decay);
    setSustain(voice, sutain);
    setRelease(voice, release);
    
}

void SID6581::setAttack(int voice, uint8_t att)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].attack=att;
    voices[voice].attack_decay=(voices[voice].attack_decay & 0x0f) +((att<<4) & 0xf0);
    pushToVoice(voice,5,voices[voice].attack_decay);
}

int SID6581::getAttack(int voice)
{
     int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+5];
    return (reg>>4);
}


void SID6581::setDecay(int voice, uint8_t decay)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].decay=decay;
    voices[voice].attack_decay=(voices[voice].attack_decay & 0xf0) +(decay & 0x0f);
    pushToVoice(voice,5,voices[voice].attack_decay);
}

int SID6581::getDecay(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+5];
    return (reg&0xf);
}

void SID6581::setSustain(int voice,uint8_t sustain)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].sustain=sustain;
    voices[voice].sustain_release=(voices[voice].sustain_release & 0x0f) +((sustain<<4) & 0xf0);
    pushToVoice(voice,6,voices[voice].sustain_release);
}


int SID6581::getSustain(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+6];
    return (reg>>4);
}

void SID6581::setRelease(int voice,uint8_t release)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].release=release;
    voices[voice].sustain_release=(voices[voice].sustain_release & 0xf0) +(release & 0x0f);
    pushToVoice(voice,6,voices[voice].sustain_release);
}

int SID6581::getRelease(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+6];
    return (reg & 0xf);
}

void SID6581::setGate(int voice, int gate)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].gate=gate;
    voices[voice].control_reg=(voices[voice].control_reg & 0xfE) + (gate & 0x1);
    pushToVoice(voice,4,voices[voice].control_reg);
    
}

int SID6581::getGate(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+4];
    return (reg & 0x1);
}


void SID6581::setTest(int voice,int test)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].test=test;
    voices[voice].control_reg=(voices[voice].control_reg & (0xff ^ SID_TEST )) + (test & SID_TEST);
    pushToVoice(voice,4,voices[voice].control_reg);
}

int SID6581::getTest(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+4];
    return ((reg & 0x8)>>3);
}

void SID6581::setSync(int voice,int sync)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].sync=sync;
    voices[voice].control_reg=(voices[voice].control_reg & (0xff ^ SID_SYNC )) + (sync & SID_SYNC);
    pushToVoice(voice,4,voices[voice].control_reg);
}


int SID6581::getSync(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+4];
    return ((reg & 0x2)>>1);
}

void SID6581::setRingMode(int voice, int ringmode)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].ringmode=ringmode;
    voices[voice].control_reg=(voices[voice].control_reg & (0xff ^ SID_RINGMODE)) + (ringmode & SID_RINGMODE);
    pushToVoice(voice,4,voices[voice].control_reg);
    
}


int SID6581::getRingMode(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+4];
    return ((reg & 0x4)>>2);
}

void SID6581::setWaveForm(int voice,int waveform)
{
//    if(voice <0 or voice >2)
//        return;
    voices[voice].waveform=waveform;
    voices[voice].control_reg= waveform + (voices[voice].control_reg & 0x01);
    pushToVoice(voice,4,voices[voice].control_reg);
    
}


int SID6581::getWaveForm(int voice)
{
    int chip=voice/3;
    uint8_t reg=sidregisters[chip*32+voice*7+4];
    return (reg >>4);
}


void SID6581::set3OFF(int chip,int _3off)
{
    sid_control[chip]._3off=_3off;
    sid_control[chip].mode_vol=(sid_control[chip].mode_vol & (0xff ^ SID_3OFF )) + (_3off & SID_3OFF);
    pushRegister(chip,0x18,sid_control[chip].mode_vol);
}
void SID6581::setHP(int chip,int hp)
{
    sid_control[chip].hp=hp;
    sid_control[chip].mode_vol=(sid_control[chip].mode_vol & (0xff ^ SID_HP )) + (hp & SID_HP);
    pushRegister(chip,0x18,sid_control[chip].mode_vol);
}
void SID6581::setBP(int chip,int bp)
{
    sid_control[chip].bp=bp;
    sid_control[chip].mode_vol=(sid_control[chip].mode_vol & (0xff ^ SID_BP )) + (bp & SID_BP);
    pushRegister(chip,0x18,sid_control[chip].mode_vol);
}
void SID6581::setLP(int chip,int lp)
{
    sid_control[chip].lp=lp;
    sid_control[chip].mode_vol=(sid_control[chip].mode_vol & (0xff ^ SID_LP )) + (lp & SID_LP);
    pushRegister(chip,0x18,sid_control[chip].mode_vol);
}

void SID6581::sidSetVolume(int chip, uint8_t vol)
{
    sid_control[chip].volume=vol;
    sid_control[chip].mode_vol=(sid_control[chip].mode_vol & 0xf0 )+( vol & 0x0F);
    pushRegister(chip,0x18,sid_control[chip].mode_vol);
}


int SID6581::getSidVolume(int chip)
{
    uint8_t reg=sidregisters[chip*32+24];
    return ((reg & 0xf));
}

void  SID6581::setFilterFrequency(int chip,int filterfrequency)
{
    sid_control[chip].filterfrequency=filterfrequency;
    sid_control[chip].fc_lo=filterfrequency & 0b111;
    sid_control[chip].fc_hi=(filterfrequency>>3) & 0xff;
    pushRegister(chip,0x15,sid_control[chip].fc_lo);
    pushRegister(chip,0x16,sid_control[chip].fc_hi);
}

void SID6581::setResonance(int chip,int resonance)
{
    sid_control[chip].res=resonance;
    sid_control[chip].res_filt=(sid_control[chip].res_filt & 0x0f) +(resonance<<4);
    pushRegister(chip,0x17,sid_control[chip].res_filt);
}

void SID6581::setFilter1(int chip,int filt1)
{
    sid_control[chip].filt1;
    sid_control[chip].res_filt=(sid_control[chip].res_filt & (0xff ^ SID_FILT1 )) + (filt1 & SID_FILT1);
    pushRegister(chip,0x17,sid_control[chip].res_filt);
}
void SID6581::setFilter2(int chip,int filt2)
{
    sid_control[chip].filt2;
    sid_control[chip].res_filt=(sid_control[chip].res_filt & (0xff ^ SID_FILT2 )) + (filt2 & SID_FILT2);
    pushRegister(chip,0x17,sid_control[chip].res_filt);
}
void SID6581::setFilter3(int chip,int filt3)
{
    sid_control[chip].filt3;
    sid_control[chip].res_filt=(sid_control[chip].res_filt & (0xff ^ SID_FILT3 )) + (filt3 & SID_FILT3);
    pushRegister(chip,0x17,sid_control[chip].res_filt);
}
void SID6581::setFilterEX(int chip,int filtex)
{
    sid_control[chip].filtex;
    sid_control[chip].res_filt=(sid_control[chip].res_filt & (0xff ^ SID_FILTEX )) + (filtex & SID_FILTEX);
    pushRegister(chip,0x17,sid_control[chip].res_filt);
}

void SID6581::soundOn()
{
    setcsw();
    setA(24);
    setD(save24);
    clearcsw(0);
    
}
unsigned int SID6581::readFile2(fs::FS &fs, const char * path)
{
    unsigned int sizet;
    Serial.printf("Reading file: %s\r\n", path);
    
    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return 0;
    }
    char buffer[30];
    unsigned int l = file.readBytesUntil('\n', buffer, sizeof(buffer));
    buffer[l] = 0;
    
    sscanf((const char *)buffer,"%lu",&sizet);
    Serial.printf("%d instructions\n",sizet);
    sidvalues=(uint8_t *)ps_malloc(sizet*4);
    if(sidvalues==NULL)
    {
        Serial.println("impossible to create memory buffer");
        return 0;
    }
    else
    {
        Serial.println("memory buffer ok");
    }
    l = file.read( sidvalues, sizet*4);//sizeof(SIDVALUES));
    return sizet;
}

void SID6581::clearcsw(int chip)
{
    //adcswrre = (adcswrre ^ (1<<WRITE) ) ^ (1<<CS) ;
    chipcontrol=0xff;
    
    switch(chip)
    {
        case 0:
            //Serial.println("jj");
                    adcswrre = (adcswrre ^ (1<<WRITE) ) ^ (1<<CS) ;
            break;
        case 1:
           // Serial.println("on chip 1");
            chipcontrol=  0xff ^ ((1<<WRITE_1) | (1<<CS_1));
            break;
        case 2:
            // Serial.println("on chip 1");
            chipcontrol=  0xff ^ ((1<<WRITE_2) | (1<<CS_2));
            break;
        case 3:
            // Serial.println("on chip 1");
            chipcontrol=  0xff ^ ((1<<WRITE_3) | (1<<CS_3));
            break;
        
        case 4:
            // Serial.println("on chip 1");
            chipcontrol=  0xff ^ ((1<<WRITE_4) | (1<<CS_4));
            break;
    }
    push();
}

void SID6581::setcsw()
{
    adcswrre = adcswrre | (1<<WRITE) | (1<<CS);
    chipcontrol= 0xff;//(1<<WRITE_1) | (1<<CS_1);;
    dataspi=0;
    push();
}

void SID6581::resetsid()
{
    cls();
    adcswrre=0;
    chipcontrol=0;
    push();
    delay(2);
    adcswrre=(1<< RESET);
    push();
    delay(2);
}


void SID6581::pushRegister(int chip,int address,int data)
{
    //Serial.printf("core p:%d\n",xPortGetCoreID());
    sidregisters[chip*32+address]=data;
    _sid_register_to_send sid_data;
    sid_data.address=address;
    sid_data.chip=chip;
    sid_data.data=data;
    xQueueSend(_sid_queue, &sid_data, portMAX_DELAY);
}



int SID6581::getRegister(int chip,int addr)
{
    return sidregisters[chip*32+addr];
}


void SID6581::_pushRegister(void *pvParameters)
{
    SID6581 * sid= (SID6581 *)pvParameters;
    
    _sid_register_to_send sid_data;
    for(;;)
    {
         xQueueReceive(_sid_queue, &sid_data, portMAX_DELAY);
        //Serial.printf("core s:%d\n",xPortGetCoreID());
        sid->setcsw();

        sid->setA(sid_data.address);
        sid->setD(sid_data.data);
        
        sid->clearcsw(sid_data.chip);
    }

}


void SID6581::pushToVoice(int voice,uint8_t address,uint8_t data)
{
    

    address=7*(voice%3)+address;
    pushRegister((int)(voice/3),address,data);

}

void SID6581::push()
{
    
    sid_spi->beginTransaction(SPISettings(sid_spiClk, LSBFIRST, SPI_MODE0));
    digitalWrite(latch_pin, 0);
    sid_spi->transfer(chipcontrol);
    sid_spi->transfer(adcswrre);
    sid_spi->transfer(dataspi);
    sid_spi->endTransaction();
    
    digitalWrite(latch_pin, 1);
    //delayMicroseconds(1);
    
}

void SID6581::setA(uint8_t val)
{
    adcswrre=((val<<3) & MASK_ADDRESS) | (adcswrre & MASK_CSWRRE);
}

void SID6581::setD(uint8_t val)
{
    dataspi=val;
}
void SID6581::cls()
{
    dataspi=0;
}
