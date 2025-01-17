#include "SidPlayer.h"
#define SID_CLOCK 25
#define SID_DATA 33
#define SID_LATCH 27
#include "SPIFFS.h"

SIDRegisterPlayer * player;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  player=new SIDRegisterPlayer();
  player->begin(SID_CLOCK,SID_DATA,SID_LATCH);
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
      player->addSong(SPIFFS,file.name()); //add all the files on the root of the spiff to the playlist
    }
    file = root.openNextFile();
  }
  player->SetMaxVolume(7); //value between 0 and 15
  player->play(); //it will play all songs in loop
}


void loop() {
  //if you jsut want to hear the songs just comment the lines below
  delay(5000);
  Serial.println("Pause the song");
  player->pausePlay();
  delay(4000);
  Serial.println("restart the song");
  player->pausePlay();
  delay(3000);
  Serial.println("hi volume");
  player->SetMaxVolume(15);
  delay(3000);
  Serial.println("low volume ");
  player->SetMaxVolume(3);
  delay(3000);
  Serial.println("medium");
   player->SetMaxVolume(7);
  delay(6000);
  Serial.println("next song");
  player->playNext(); //sid.playPrev(); if you want to go backwards
  delay(10000);
  //player->stopPlayer(); //to stop the plater completely
  //delay(10000);
  //player->play(); //to restart it
}
