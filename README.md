This library is to control SID 6581 chips from the 1980s era with an Esp32.
the program allows you to push directly the register to the  SID chip. hence you can program like in the good all times :)
it should work with other mcu as it uses SPI but not tested.
NB: playSIDTunes will only work with WROOVER because I use PSRAM for the moment. all the rest will run with all esp32.
I intend on writing a MIDI translation to SID chip. Hence a Play midi will be availble soon.

look at the schematics for the setup of the shift registers and  MOS 6581

Example to play a SIDtunes
```
#include "SID6581.h"
#define SID_CLOCK 25
#define SID_DATA 33
#define SID_LATCH 27
#include "SPIFFS.h"
SID6581 sid;

void setup() {
// put your setup code here, to run once:
    Serial.begin(115200);

    sid.begin(SID_CLOCK,SID_DATA,SID_LATCH);

    if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Mount Failed");
    return;
    }


//the following line will go through all the files in the SPIFFS
//Do not forget to do "Tools-> ESP32 Scketch data upload"
File root = SPIFFS.open("/");
if(!root){
    Serial.println("- failed to open directory");
    return;
}
if(!root.isDirectory()){
    Serial.println(" - not a directory");
    return;
}

File file = root.openNextFile();
while(file){
if(file.isDirectory()){

} else {
    Serial.print(" add file  FILE: ");
    Serial.print(file.name());
    Serial.print("\tSIZE: ");
    Serial.println(file.size());
    sid.addSong(SPIFFS,file.name()); //add all the files on the root of the spiff to the playlist
}
    file = root.openNextFile();
}
sid.sidSetVolume(7); //value between 0 and 15


sid.play(); //it will play all songs in loop
}

void loop() {
//if you jsut want to hear the songs just comment the lines below


        delay(5000);
        Serial.println("Pause the song");
        sid.pausePlay();
        delay(4000);
        Serial.println("restart the song");
        sid.pausePlay();
        delay(3000);
        Serial.println("hi volume");
        sid.sidSetVolume(15);
        delay(3000);
        Serial.println("low volume ");
        sid.sidSetVolume(3);
        delay(3000);
        Serial.println("medium");
        sid.sidSetVolume(7);
        delay(3000);

        delay(3000);
        Serial.println("nexxt song");
        sid.playNext(); //sid.playPrev(); if you want to go backwards
        delay(10000);

        //sid.stopPlayer(); //to stop the plater completly
        //delay(10000);
        //sid.play(); //to restart it

}
```



PS: to transform the .sid into register commands 

1) I use the fantastic program of Ken Händel
https://haendel.ddns.net/~ken/#_latest_beta_version
```
java -jar jsidplay2_console-4.1.jar --audio LIVE_SID_REG --recordingFilename export_file sid_file

use SID_REG instead of LIVE_SID_REG to keep you latptop quiet


```
2) I use the program traduct_2.c
to compile it for your platform:
```
>gcc traduct_2.c -o traduct
```
to use it
```
./traduct export_file > final_file

```
Put the final_file on a SD card or in your SPIFF

here it goes

Updated 6 March 2020