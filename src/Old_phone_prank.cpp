/*
Bakelite phone sketch
Old telephone, with WEMOS controlling the coils (2) for ringing via MOSFET
Handset pickup switch, and LEDs mounted under phone and SDCard music player

Prank starts with either a BLYNK call to make phone ring,
then once (or if without ringing) handset lifted, LEDs flash and music is played
through the ear piece, and a small speaker

mp3_play ();		//start play
mp3_play (5);	    //play "mp3/0005.mp3"
mp3_next ();		//play next 
mp3_prev ();		//play previous
mp3_set_volume (uint16_t volume);	//0~30
mp3_set_EQ ();	//0~5
mp3_pause ();
mp3_stop ();
void mp3_get_state (); 	//send get state command
void mp3_get_volume (); 
void mp3_get_u_sum (); 
void mp3_get_tf_sum (); 
void mp3_get_flash_sum (); 
void mp3_get_tf_current (); 
void mp3_get_u_current (); 
void mp3_get_flash_current (); 
void mp3_single_loop (boolean state);	//set single loop 
void mp3_DAC (boolean state); 
void mp3_random_play (); 
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <FastLED.h>
#include <SoftwareSerial.h>    //Library for serial comms to TF Sound module
#include <DFPlayer_Mini_Mp3.h> //Library for TF Sound module

#define NUM_LEDS_PER_STRIP 18 //Number of LEDs per strip
#define COLOR_ORDER GRB       // LED stips aren't all in the same RGB order.  If colours are wrong change this  e.g  RBG > GRB
#define PIN_LED D7            //GPIO13 - FAST.LED Pin

#define BLYNK_PRINT Serial

#define PIN_PICKUP D3 //GPIO 0 - Handset switch, LOW = Handset lifted
#define PIN_COIL1 D5  //GPIO14 - Coil 1
#define PIN_COIL2 D6  //GPIO12 - Coil 2

SoftwareSerial mySerial(4, 5); // Declare pin RX & TX pins for TF Sound module.  Using D1 (GPIO 5) and D2 (GPIO 4)

//build_flags =
//    -DSSID_NAME="SSID"
//    -DPASSWORD_NAME="password"
//    -DBLYN
#ifndef BLYNKCERT_NAME
#define BLYNKCERT_NAME "1234567890" //Default BLYNK Cert if not build flag from PlatformIO doesn't work
#endif

#ifndef SSID_NAME
#define SSID_NAME "WIFI_SSID" //Default SSID if not build flag from PlatformIO doesn't work
#endif

#ifndef PASSWORD_NAME
#define PASSWORD_NAME "WIFI_PASSWORD" //Default WiFi Password if not build flag from PlatformIO doesn't work
#endif

//Gets SSID/PASSWORD from PlatformIO.ini build flags
const char ssid[] = xstr(SSID_NAME);      //  your network SSID (name)
const char pass[] = xstr(PASSWORD_NAME);  // your network password
const char auth[] = xstr(BLYNKCERT_NAME); // your BLYNK Cert

//Ring variables
int coil_delay = 17;       //Delay for coil to work and switch off
int ring_delay = 400;      //ring <small pause> ring
int ring_gap_delay = 1600; //pause amount between ring ring <longer pause> ring ring
int ringcycle = 0;         //Counts the number of rings
int ringcount = 4;         //Number of rings per ring burst
int ringflip = 1;          //After bells sound x (ringcount) times, and a pause, then next pause is 2x longer
int bounce_delay = 50;     //Try and remove handset bounce
int ringphase = 0;         //1= Make it ring
int giveuprings = 50;      //How many times to ring before giving up
int giveupringscount = 0;  //Counter for number of rings before giving up
int playphase = 0;         //1=Sound on, and LEDs turn on

bool handset;          //Handset up or down
int pickup_hangup = 0; //0 = No change, 1 = Just put down, 2 = Just picked up

int red = 255;          //LED Colour on/off
int green = 255;        //LED Colour on/off
int blue = 255;         //LED Colour on/off
int LED_loop_count = 1; //Counter for how many times to loop before updating LEDs in playmode
int LED_loop = 1500;    //how many times to loop before updating LEDs in playmode

int mp3vol = 30;      //Volume for DF card player
int mp3_selected = 1; //Default mp3 to play (0001.mp3)

void Handset_Interupt();
void Prank_Interupt();
void prank();
void MakeitRing();

//Setup for FastLED and BLYNK
CRGB leds[NUM_LEDS_PER_STRIP];
WidgetLCD lcd(V2);

//BLYNK Definition to capture virtual pin to start prank
BLYNK_WRITE(V1)
{
    if (param.asInt() == 1)
    {
        ; // assigning incoming value from pin V1 to a variable
        giveupringscount = 0;
        ringphase = 1; //turn on rings
        playphase = 0; //make sure LEDs and sound off
        Serial.println("Start PRANK");
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(4, 0, "Ring Ring...");
    }
}

//BLYNK Definition to capture what mp3 file to play via Virtual pin 3 and drop down Blynk control
BLYNK_WRITE(V3)
{
    mp3_selected = param.asInt();
}

void setup()
{
    Serial.begin(9600);

    //DF Player setup
    mySerial.begin(9600);     //Initiate comms to TF Sound module
    mp3_set_serial(mySerial); //set softwareSerial for DFPlayer-mini mp3 module
    mp3_set_volume(mp3vol);   //Set default volume
    mp3_stop();               //soft-Reset module DFPlayer.  Make sure nothing is playing on start up

    FastLED.addLeds<WS2812, PIN_LED, COLOR_ORDER>(leds, NUM_LEDS_PER_STRIP);

    Blynk.begin(auth, ssid, pass);

    //Set pin out direction
    pinMode(PIN_COIL1, OUTPUT); //Output for Coil1
    pinMode(PIN_COIL2, OUTPUT); //Output for Coil2
    pinMode(PIN_PICKUP, INPUT); //Input for handset switch
    pinMode(PIN_LED, OUTPUT);   //Output for FastLED

    attachInterrupt(digitalPinToInterrupt(PIN_PICKUP), Handset_Interupt, CHANGE); //Any change in the handset state trigger the interupt

    Serial.println("Set up done....ready to for fun...");
}

void loop()
{
    Blynk.run();

    //Loop, each time check if LED/Sound or Ringing should be activiated
    //Play sound and LEDs phase.  Handset has been lifted
    //Sounds will loop until sonuds board button toggled to stop

    //Keep giveuprings = 0 if not in ringphase
    if (ringphase == 0)
    {
        giveupringscount = 0;
    }

    if (pickup_hangup == 2 && playphase == 0)
    {
        Serial.println("Pick up");
        playphase = 1; //Turn on LEDs and Sounds
        ringphase = 0; //Turn off the ringing
        giveupringscount = 0;

        mp3_set_volume(mp3vol); //Set the mp3 volume
        mp3_play(mp3_selected); //Play selected mp3 in folder mp3

        Serial.println("Start sounds");
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(4, 0, "Start sounds");
    }

    if (pickup_hangup == 1 && playphase == 1)
    {
        Serial.println("Hang up");
        playphase = 0; //Turn off LEDs and Sounds
        ringphase = 0; //Turn off the ringing
        giveupringscount = 0;
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(3, 0, "Hang up");

        mp3_stop();

        Serial.println("Stop sounds");
        Serial.println("....ready to for fun again...");
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(3, 0, "Stop sounds");
        delay(500);
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(4, 0, "Ready..");
    }

    //Do LEDs
    if (playphase == 1)
    {

        if (LED_loop_count == LED_loop)
        {                       //Only update LEDs every Nth loop
            LED_loop_count = 1; //reset the counter
            fill_solid(&(leds[0]), NUM_LEDS_PER_STRIP, CRGB(red * random(0, 2), green * random(0, 2), blue * random(0, 2)));
            FastLED.show();
        }
        else
            LED_loop_count = LED_loop_count + 1; //Increment the counter
    }

    //Turn off LEDs
    if (playphase == 0)
    {
        fill_solid(&(leds[0]), NUM_LEDS_PER_STRIP, CRGB(0, 0, 0));
        FastLED.show();
    }

    //Ringing phase sequence.  Prank has been started
    if (ringphase == 1)
    {
        MakeitRing();
    }
}

void Handset_Interupt()
{
    //Function run with PIN_PICKUP = LOW, now check again 200ms later

    //Bounce delay otherwise it gets confused
    int millisstart;
    millisstart = millis();
    while (millis() < millisstart + bounce_delay)
    {
    }

    handset = digitalRead(PIN_PICKUP); //Read handset state after bounce delay
    Serial.print("handset = ");
    Serial.println(handset);

    //Set ringphase = 0 (off)  -  disable power to coils
    //Set playLEDvalue = 1 (on)  - Play sounds and turn LEDs on
    //If handset ==0 and playphase already = 1 then do nothing (we've already actioned)

    if (handset == 1 && playphase == 1)
    {
        pickup_hangup = 1; //Handset just put down
    }

    if (handset == 0 && playphase == 0)
    {
        pickup_hangup = 2; //Handset just picked up
    }
}

void MakeitRing()
{
    Serial.print("Ring Ring: ");
    Serial.println(giveupringscount);
    //Ring each bell once checking if handset has been lifted

    int millisstart; //used to count millis for delay between rings.  Avoid using delay function

    digitalWrite(PIN_COIL1, ringphase);

    millisstart = millis(); //wait for coil
    while (millis() < millisstart + coil_delay)
    {
        yield();
    } //avoid WDT expections

    digitalWrite(PIN_COIL1, LOW);

    millisstart = millis(); //wait for coil
    while (millis() < millisstart + coil_delay)
    {
        yield();
    } //avoid WDT expections

    digitalWrite(PIN_COIL2, ringphase);

    millisstart = millis(); //wait for coil
    while (millis() < millisstart + coil_delay)
    {
        yield();
    } //avoid WDT expections

    digitalWrite(PIN_COIL2, LOW);

    millisstart = millis(); //wait for coil
    while (millis() < millisstart + coil_delay)
    {
        yield();
    } //avoid WDT expections

    //Ring bell x times then pause
    ringcycle = ringcycle + 1;

    //work out the delay for a short burst, delay, short burst, longer delay...repeat
    if (ringcycle == ringcount)
    {
        ringcycle = 0;
        millisstart = millis(); //wait for coil
        while (millis() < millisstart + ring_delay)
        {
            yield();
        } //avoid WDT expections

        ringflip = ringflip * -1;

        if (ringflip == -1)
        {
            ringcycle = 0;
            millisstart = millis(); //wait for coil
            while (millis() < millisstart + ring_gap_delay)
            {
                yield();
            } //avoid WDT expections
        }
    }

    //Do a quick action on handset state - change trigger variables as needed
    //check_handset();

    //Ring only so many times before going back into wait state
    giveupringscount = giveupringscount + 1;

    if (giveupringscount > giveuprings)
    {
        giveupringscount = 0;
        ringphase = 0;
        Serial.println("Giving up on rings...no one picked up (grump)");
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(4, 0, "Giving up");
        delay(1000);
        lcd.clear(); //Use it to clear the LCD Widget
        lcd.print(4, 0, "Ready..");
        Serial.println("....ready to for fun again...");
    }
}
