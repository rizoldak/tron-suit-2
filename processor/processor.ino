#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#include <OneWire.h> //crc8 
#include <cobs.h> //cobs encoder and decoder 
#include <OctoWS2811.h> //DMA strip output
#include <Metro.h> //timers

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "FastLED.h" //for HSV Libraries only, not for output!

typedef struct {
	uint8_t color_sensor_R;
	uint8_t color_sensor_G;
	uint8_t color_sensor_B;
} SINGLELED;




typedef struct {
	boolean fresh;
	boolean finger1_in_progress;
	boolean finger2_in_progress;
	boolean finger3_in_progress;
	boolean finger4_in_progress;

	int16_t yaw_raw;  //yaw pitch and roll in degrees * 100
	int16_t pitch_raw;
	int16_t roll_raw;

	int32_t yaw_offset;  //yaw pitch and roll in degrees * 100
	int32_t pitch_offset;
	int32_t roll_offset;

	int32_t yaw_compensated;  //yaw pitch and roll in degrees * 100
	int32_t pitch_compensated;
	int32_t roll_compensated;

	int16_t aaRealX; // gravity-free accel sensor measurements
	int16_t aaRealY;
	int16_t aaRealZ;

	int16_t gravityX; // world-frame accel sensor measurements
	int16_t gravityY;
	int16_t gravityZ;

	int16_t color_sensor_R; // world-frame accel sensor measurements
	int16_t color_sensor_G;
	int16_t color_sensor_B;
	int16_t color_sensor_Clear;

	boolean finger1;
	boolean finger2;
	boolean finger3;
	boolean finger4;

	uint8_t packets_in_per_second;
	uint8_t packets_out_per_second;

	uint8_t framing_errors;
	uint8_t crc_errors;

	uint8_t cpu_usage;
	uint8_t cpu_temp;

	//outputs
	uint8_t LED_R;
	uint8_t LED_G;
	uint8_t LED_B;

	uint8_t color_sensor_LED;

	int8_t gloveX;
	int8_t gloveY;

} GLOVE;

typedef struct {
	uint8_t packets_in_per_second;
	uint8_t packets_out_per_second;

	uint8_t framing_errors;
	uint8_t crc_errors;

} SERIALSTATS;

byte gammatable[256];// double or triple this later.





const int ledsPerStrip = 128;

DMAMEM int displayMemory[ledsPerStrip * 6];
int drawingMemory[ledsPerStrip * 6];

const int config = WS2811_GRB | WS2811_800kHz;

byte helmet_LED_R = 0xff;
byte helmet_LED_G = 0;
byte helmet_LED_B = 0;

#define GLOVE_DEADZONE 3000  //30 degrees
#define GLOVE_MAXIMUM 30000 //90 degrees


#define OLED_DC     17
#define OLED_CS     22
#define OLED_RESET  19
Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);

uint8_t menu_mode = 0;
uint8_t menu_text_length = 0;
uint8_t scroll_count = 0;
int8_t scroll_pos_x = 0;
int8_t scroll_pos_y = 0;
uint8_t scroll_mode = 0;
long int scroll_timer = 0;

//crank up hardwareserial.cpp to 128 to match!
#define INCOMING1_BUFFER_SIZE 128
uint8_t incoming1_raw_buffer[INCOMING1_BUFFER_SIZE];
uint8_t incoming1_index = 0;
uint8_t incoming1_decoded_buffer[INCOMING1_BUFFER_SIZE];

AudioInputAnalog         adc1(A9);
AudioAnalyzeFFT256       fft256_1;
AudioConnection          patchCord1(adc1, fft256_1);

OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config);

#define TESTING

unsigned long fps_time = 0;
int fpscount = 0;
GLOVE glove0;
GLOVE glove1;
SERIALSTATS serial1stats;

byte gloveindicator[16][8];

uint32_t EQdisplay[16][8];
int EQdisplayValueMax[16]; //max vals for normalization over time

Metro FPSdisplay = Metro(1000);
Metro glovedisplayfade = Metro(10);
Metro YPRdisplay = Metro(100);
Metro ScrollSpeed = Metro(40);
Metro GloveSend = Metro(10);

void setup() {
	for (int i = 0; i < 256; i++) {
		float x = i;
		x /= 255;
		x = pow(x, 2.5);
		x *= 255;

		if (0) {
			gammatable[i] = 255 - x;
		}
		else {
			gammatable[i] = x;
		}
		//Serial.println(gammatable[i]);
	}



	display.begin(SSD1306_SWITCHCAPVCC);

	display.display();

	glove1.finger1_in_progress = false;
	glove0.finger1_in_progress = false;
	glove0.fresh = false;
	glove1.fresh = false;
	glove1.yaw_offset = 0;
	glove1.pitch_offset = 0;
	glove1.roll_offset = 0;
	glove0.yaw_offset = 0;
	glove0.pitch_offset = 0;
	glove0.roll_offset = 0;
	glove0.LED_R = 0;
	glove0.LED_G = 0;
	glove0.LED_B = 0;
	glove0.color_sensor_LED = 0;
	glove1.LED_R = 0;
	glove1.LED_G = 0;
	glove1.LED_B = 0;
	glove1.color_sensor_LED = 0;

	//must go first so Serial.begin can override pins!!!
	leds.begin();

	//bump hardwareserial.cpp to 255
	Serial.begin(115200);   //USB Debug
	Serial1.begin(115200);  //Gloves	
	Serial2.begin(115200);  //color_sensor_Btooth	
	Serial3.begin(115200);  //Xbee	

	pinMode(A4, INPUT); //audio input

	AudioMemory(4);
	fft256_1.windowFunction(AudioWindowHanning256);
	fft256_1.averageTogether(4);

	leds.show();
}

long int magictime = 0;
int ledmodifier = 1;
int indexled = 0;

#define menu_location_x 1
#define menu_location_y 11

#define realtime_location_x 19
#define realtime_location_y 1

void loop() {
	fpscount++;
	if (glove1.finger2 == 0 || glove1.finger3 == 0){
		if (glove1.finger2 == 0){
			glove0.color_sensor_LED = 0x01;
		}
		uint32_t sum = glove0.color_sensor_Clear;
		float r, g, b;
		r = glove0.color_sensor_R; r /= sum;
		g = glove0.color_sensor_G; g /= sum;
		b = glove0.color_sensor_B; b /= sum;
		r *= 256; g *= 256; b *= 256;

		glove0.LED_R = (int)r;
		glove0.LED_G = (int)g;
		glove0.LED_B = (int)b;


		helmet_LED_R = glove0.LED_R;
		helmet_LED_G = glove0.LED_G;
		helmet_LED_B = glove0.LED_B;

	}
	else{


		glove0.color_sensor_LED = 0x00;
		glove0.LED_R = 0;
		glove0.LED_G = 0;
		glove0.LED_B = 0;

	}



	if (Serial2.available()){
		Serial2.write(Serial2.read());
		indexled = 0;
	}

	if (Serial3.available()){
		Serial3.write(Serial3.read());
		indexled = 0;
	}


	if (glovedisplayfade.check()){
		for (int x = 0; x < 16; x++) {
			for (int y = 0; y < 8; y++) {
				if (gloveindicator[x][y] > 0){
					gloveindicator[x][y] = gloveindicator[x][y] - 1;
				}
			}
		}
	}

	if (glove1.finger1 == 0){
		gloveindicator[8 + glove0.gloveX][7 - glove0.gloveY] = 100;// flip Y for helmet external display by subtracting from 7
		gloveindicator[glove1.gloveX][7 - glove1.gloveY] = 100;// flip Y for helmet external display by subtracting from 7
	}



	//HUD!

	display.clearDisplay();


	//crosshair 
	display.drawRect(realtime_location_x - 1, realtime_location_y - 1, 18, 10, WHITE);
	display.drawRect(0, 0, 19, 10, WHITE);
	display.drawRect(menu_location_x - 1, menu_location_y - 1, 19, 10, WHITE);
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 8; y++) {
			if (gloveindicator[x][y] == 100){
				display.drawPixel(x + 1, y, WHITE);
			}
		}
	}

	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(menu_location_x, menu_location_y);

	display.setTextWrap(false);

	display.print(menu_mode);
	menu_text_length = display.getCursorX();

	for (uint8_t y = 0; y < 8; y++) {
		for (uint8_t x = 0; x < 16; x++) {

			uint32_t background_array = 0;
			uint32_t menu_name = 0;
			uint32_t final_color = 0;

			if (scroll_mode > 0){
				if (read_menu_pixel(x, y) == 1){ // flip Y for helmet external display by subtracting from 7
					menu_name = (uint32_t)gammatable[255] << 16 | (uint32_t)gammatable[255] << 8 | (uint32_t)gammatable[255];
				}
			}

			if (gloveindicator[x][7 - y] > 0){ // flip Y for helmet external display by subtracting from 7
				background_array = (uint32_t)gammatable[gloveindicator[x][7 - y]] << 16 | (uint32_t)gammatable[gloveindicator[x][7 - y]] << 8 | (uint32_t)gammatable[gloveindicator[x][7 - y]];
			}

			uint8_t tempindex;  //HUD is only 128 LEDs so it will fit in a byte

			if (y & 0x01 == 1){ // for odd rows scan from opposite side since external display is zig zag wired
				tempindex = (y << 4) + 15 - x;  //  << 4 is multiply by 16 pixels per row
			}
			else{
				tempindex = (y << 4) + x; //  << 4 is multiply by 16 pixels per row
			}

			//blackout behind text
			//if ((scroll_count > 0) && ((scroll_pos_x + x) >= 0) && ((scroll_pos_x + x) <= menu_text_length)){

			final_color = background_array;

				if (read_menu_pixel(x, y) != 3){
					final_color = menu_name;
				}


			
			final_color = final_color | EQdisplay[x][y];

			if ((final_color & 0xff == 0xff) | ((final_color >> 16) & 0xff == 0xff) | ((final_color >> 8) & 0xff == 0xff)){
				display.drawPixel(realtime_location_x + x, realtime_location_y + 7 - y, WHITE);
			}

			leds.setPixel(tempindex, final_color);
		}
	}

	//advance scrolling if timer reached or stop

	if (ScrollSpeed.check()){
		//center the text from whatever direction its coming from
		switch (scroll_mode){
		case 3:
			if (scroll_pos_x > 0) scroll_pos_x--;
			if (scroll_pos_y > 0) scroll_pos_y--;
			if (scroll_pos_x < 0) scroll_pos_x++;
			if (scroll_pos_y < 0) scroll_pos_y++;
			if (scroll_pos_y == 0 && scroll_pos_x == 0){
				scroll_mode = 2;
				scroll_timer = millis();
			}
			break;
		case 2:
			if (millis() - scroll_timer > 1000){
				scroll_mode = 1;
			}
			break;
		case 1:
			//scroll off the left side
			scroll_pos_x++;
			if (scroll_pos_x > menu_text_length){
				scroll_mode = 0;
			}
			break;
		}
	}

	display.display();
	leds.show();

	if (GloveSend.check()){

		uint8_t raw_buffer[9];

		raw_buffer[0] = glove0.LED_R;
		raw_buffer[1] = glove0.LED_G;
		raw_buffer[2] = glove0.LED_B;
		raw_buffer[3] = glove0.color_sensor_LED;
		raw_buffer[4] = glove1.LED_R;
		raw_buffer[5] = glove1.LED_G;
		raw_buffer[6] = glove1.LED_B;
		raw_buffer[7] = glove1.color_sensor_LED;

		raw_buffer[8] = OneWire::crc8(raw_buffer, 7);

		uint8_t encoded_buffer[9];
		uint8_t encoded_size = COBSencode(raw_buffer, 9, encoded_buffer);


		Serial1.write(encoded_buffer, encoded_size);
		Serial1.write(0x00);

		if (serial1stats.packets_out_per_second < 255){
			serial1stats.packets_out_per_second++;
		}
	}

	if (YPRdisplay.check()){

		display.reinit();

		if (0){
			Serial.print("ypr\t");
			Serial.print(sin(glove1.roll_raw * PI / 18000));
			Serial.print("ypr\t");
			Serial.print(glove1.finger1);
			Serial.print("\t");
			Serial.print(glove1.yaw_compensated);
			Serial.print("\t");
			Serial.print(glove1.pitch_compensated);
			Serial.print("\t");
			Serial.print(glove1.roll_compensated);
			Serial.print("\t");
			Serial.print(glove1.yaw_raw);
			Serial.print("\t");
			Serial.print(glove1.pitch_raw);
			Serial.print("\t");
			Serial.print(glove1.roll_raw);
			Serial.print("\t");
			Serial.print(glove1.yaw_offset);
			Serial.print("\t");
			Serial.print(glove1.pitch_offset);
			Serial.print("\t");
			Serial.println(glove1.roll_offset);
		}
	}


	if (FPSdisplay.check()){
		Serial.println(fpscount);
		fpscount = 0;
		//cpu_usage = 100 - (idle_microseconds / 10000);
		//local_packets_out_per_second_1 = local_packets_out_counter_1;
		//local_packets_in_per_second_1 = local_packets_in_counter_1;
		//idle_microseconds = 0;
		// local_packets_out_counter_1 = 0;
		// local_packets_in_counter_1 = 0;
	}


	if (fft256_1.available()) {

		uint8_t fftmode = 2;

		if (menu_mode == 20){
			fftmode = 0;
		}

		if (fftmode == 0){
			//move eq data left 1
			for (uint8_t x = 1; x < 16; x++) {
				for (uint8_t y = 0; y < 8; y++) {
					EQdisplay[x - 1][y] = EQdisplay[x][y];
				}
			}
		}

		if (fftmode == 1){
			//move eq data right 1
			for (uint8_t x = 15; x > 0; x--) {
				for (uint8_t y = 0; y < 8; y++) {
					EQdisplay[x][y] = EQdisplay[x - 1][y];
				}
			}
		}

		if (fftmode == 1 || fftmode == 0){
			for (uint8_t i = 0; i < 8; i++) {

				uint16_t n = 1000 * fft256_1.read((i * 5), (i * 5) + 5);

				switch (i){
				case 0:  n = max(n - 150, 0); break;
				case 1:  n = max(n - 15, 0);  break;
				case 2:  n = max(n - 6, 0);   break;
				default: n = max(n - 5, 0);   break;
				}

				//Serial.print((int)(n));
				//Serial.print(' ');

				EQdisplayValueMax[i] = max(max(EQdisplayValueMax[i] * .99, n), 10);

				uint8_t brightness = map(n, 0, EQdisplayValueMax[i], 0, 255);
				uint16_t color = 0;

				color = constrain(map(brightness, 230, 250, 0, 255), 0, 255);

				if (fftmode == 0){

					EQdisplay[15][i] = wheel(color, 0, brightness);
				}
				if (fftmode == 1){
					EQdisplay[0][i] = wheel(min(color, 255), 0, brightness);
				}
			}
			//Serial.println("");
		}

		if (fftmode == 2 || fftmode == 3){
			for (uint8_t i = 0; i < 16; i++) {

				uint16_t n = 1000 * fft256_1.read((i * 2), (i * 2) + 2);

				switch (i){
				case 0:  n = max(n - 150, 0); break;
				case 1:  n = max(n - 50, 0);  break;
				case 2:  n = max(n - 15, 0);  break;
				case 3:  n = max(n - 10, 0);  break;
				default: n = max(n - 3, 0);   break;
				}


				//Serial.print((int)(n));
				//Serial.print(' ');

				EQdisplayValueMax[i] = max(max(EQdisplayValueMax[i] * .98, n), 4);

				//int y = map(n, 0, EQdisplayValueMax[i], 0, 8);
				int brightness = map(n, 0, EQdisplayValueMax[i], 0, 255);
				int color = 0;
				int offset = 127;

				color = constrain(map(brightness, 230, 255, 0, 255), 0, 255);

				for (int index = 0; index < 8; index++){
					EQdisplay[i][index] = wheel(color, 0, brightness);
				}
			}
			//Serial.println("");
		}
	}

	SerialUpdate();
}

uint8_t read_menu_pixel(uint8_t x, uint8_t y){


	//bounds check first
	if (x + scroll_pos_x < 0 || x + scroll_pos_x > 15 || scroll_pos_y + (7 - y) < 0 || scroll_pos_y + (7 - y) > 7){
		return 2;
	}

	//add 7 sub y to flip on y axis
	if (display.readPixel(menu_location_x + scroll_pos_x + x, menu_location_y + scroll_pos_y + (7 - y))){
		return 1;
	}
	else{
		return 0;
	}
}


#define MODECHANGEBRIGHTNESS 250  
#define MODECHANGESTARTPOSX -18
#define MODECHANGESTARTPOSY 8

void readglove(void * temp){

	GLOVE * current_glove = (GLOVE *)temp;
	if (current_glove->finger1 == 1 && current_glove->finger1_in_progress == true){

		if (current_glove->gloveY <= 0){
			Serial.println(" down!");


			scroll_mode = 3;
			scroll_pos_x = 0;
			scroll_pos_y = MODECHANGESTARTPOSY;

			if (current_glove == &glove1){
				gloveindicator[0][4] = MODECHANGEBRIGHTNESS;
				gloveindicator[1][5] = MODECHANGEBRIGHTNESS;
				gloveindicator[2][6] = MODECHANGEBRIGHTNESS;
				gloveindicator[3][7] = MODECHANGEBRIGHTNESS;
				gloveindicator[4][7] = MODECHANGEBRIGHTNESS;
				gloveindicator[5][6] = MODECHANGEBRIGHTNESS;
				gloveindicator[6][5] = MODECHANGEBRIGHTNESS;
				gloveindicator[7][4] = MODECHANGEBRIGHTNESS;
			}
			//root menus
			if (menu_mode == 3){
				menu_mode = 2;
			}
			else if (menu_mode == 2){
				menu_mode = 1;
			}
			else if (menu_mode == 1){
				menu_mode = 3;
			}


			//10s menus
			if (menu_mode >= 10 && menu_mode <= 19){
				menu_mode--;

				//rollaround
				if (menu_mode == 9){
					menu_mode = 13;
				}
			}

		}
		else
		{
			if (current_glove->gloveY >= 7){
				Serial.println(" up!");

				scroll_mode = 3;
				scroll_pos_x = 0;
				scroll_pos_y = -MODECHANGESTARTPOSY;

				if (current_glove == &glove1){
					gloveindicator[0][3] = MODECHANGEBRIGHTNESS;
					gloveindicator[1][2] = MODECHANGEBRIGHTNESS;
					gloveindicator[2][1] = MODECHANGEBRIGHTNESS;
					gloveindicator[3][0] = MODECHANGEBRIGHTNESS;
					gloveindicator[4][0] = MODECHANGEBRIGHTNESS;
					gloveindicator[5][1] = MODECHANGEBRIGHTNESS;
					gloveindicator[6][2] = MODECHANGEBRIGHTNESS;
					gloveindicator[7][3] = MODECHANGEBRIGHTNESS;



				}

				//root menus
				if (menu_mode == 1){
					menu_mode = 2;
				}
				else if (menu_mode == 2){
					menu_mode = 3;
				}
				else if (menu_mode == 3){
					menu_mode = 1;
				}

				//10s menus
				if (menu_mode >= 10 && menu_mode <= 19){
					menu_mode++;

					//rollaround
					if (menu_mode == 14){
						menu_mode = 10;
					}
				}



			}

			else{
				if (current_glove->gloveX <= 0){
					Serial.println(" right!");

					scroll_mode = 3;
					scroll_pos_x = MODECHANGESTARTPOSX;
					scroll_pos_y = 0;

					if (current_glove == &glove1){
						gloveindicator[3][0] = MODECHANGEBRIGHTNESS;
						gloveindicator[2][1] = MODECHANGEBRIGHTNESS;
						gloveindicator[1][2] = MODECHANGEBRIGHTNESS;
						gloveindicator[0][3] = MODECHANGEBRIGHTNESS;
						gloveindicator[0][4] = MODECHANGEBRIGHTNESS;
						gloveindicator[1][5] = MODECHANGEBRIGHTNESS;
						gloveindicator[2][6] = MODECHANGEBRIGHTNESS;
						gloveindicator[3][7] = MODECHANGEBRIGHTNESS;
					}


					//jump into first layer menu
					if (menu_mode < 10){
						menu_mode = menu_mode * 10;
					}


				}

				if (current_glove->gloveX >= 7){
					Serial.println(" left!");

					scroll_mode = 3;
					scroll_pos_x = min(menu_text_length, 16);
					scroll_pos_y = 0;

					if (current_glove == &glove1){
						gloveindicator[4][0] = MODECHANGEBRIGHTNESS;
						gloveindicator[5][1] = MODECHANGEBRIGHTNESS;
						gloveindicator[6][2] = MODECHANGEBRIGHTNESS;
						gloveindicator[7][3] = MODECHANGEBRIGHTNESS;
						gloveindicator[7][4] = MODECHANGEBRIGHTNESS;
						gloveindicator[6][5] = MODECHANGEBRIGHTNESS;
						gloveindicator[5][6] = MODECHANGEBRIGHTNESS;
						gloveindicator[4][7] = MODECHANGEBRIGHTNESS;
					}

					//back out of 10s menu
					if (menu_mode <= 19 && menu_mode >= 10){
						menu_mode = 1;
					}

					//back out of 20s menu
					if (menu_mode <= 29 && menu_mode >= 20){
						menu_mode = 2;
					}

					//back out of 30s menu
					if (menu_mode <= 39 && menu_mode >= 30){
						menu_mode = 3;
					}

					//back out of 40s menu
					if (menu_mode <= 49 && menu_mode >= 40){
						menu_mode = 4;
					}

				}
			}
		}
	}

	if (current_glove->finger4 == 0){
		if (current_glove->finger4_in_progress == false){
			current_glove->finger4_in_progress = true;

			if (menu_mode == 0){
				menu_mode = 1;

			}
			else{
				menu_mode = 0;

			}

		}
	}
	else{
		current_glove->finger4_in_progress = false;


	}

	if (current_glove->finger1 == 0){
		if (current_glove->finger1_in_progress == false){
			current_glove->finger1_in_progress = true;
			current_glove->yaw_offset = current_glove->yaw_raw;
			current_glove->pitch_offset = current_glove->pitch_raw;
			current_glove->roll_offset = current_glove->roll_raw;

			//TEMP
			glove0.yaw_offset = glove0.yaw_raw;
			glove0.pitch_offset = glove0.pitch_raw;
			glove0.roll_offset = glove0.roll_raw;

		}
	}
	else{
		current_glove->finger1_in_progress = false;
	}
}


void SerialUpdate(void){

	while (Serial1.available()){

		//read in a byte
		incoming1_raw_buffer[incoming1_index] = Serial1.read();

		//check for end of packet
		if (incoming1_raw_buffer[incoming1_index] == 0x00){
			//try to decode
			uint8_t decoded_length = COBSdecode(incoming1_raw_buffer, incoming1_index, incoming1_decoded_buffer);

			//check length of decoded data (cleans up a series of 0x00 bytes)
			if (decoded_length > 0){
				onPacket1(incoming1_decoded_buffer, decoded_length);
			}

			//reset index
			incoming1_index = 0;
		}
		else{
			//read data in until we hit overflow, then hold at last position
			if (incoming1_index < INCOMING1_BUFFER_SIZE)
				incoming1_index++;
		}
	}
}

void onPacket1(const uint8_t* buffer, size_t size)
{

	if (size != 35){
		serial1stats.framing_errors++;
	}
	else{
		uint8_t crc = OneWire::crc8(buffer, size - 2);
		if (crc != buffer[size - 1]){
			serial1stats.crc_errors++;
		}
		else{

			if (serial1stats.packets_in_per_second < 255){
				serial1stats.packets_in_per_second++;
			}

			GLOVE * current_glove;
			if (buffer[33] == 0){
				current_glove = &glove0;
			}
			else if (buffer[33] == 1)
			{
				current_glove = &glove1;
			}

			current_glove->fresh = true;

			current_glove->yaw_raw = buffer[0] << 8 | buffer[1];
			current_glove->pitch_raw = buffer[2] << 8 | buffer[3];
			current_glove->roll_raw = buffer[4] << 8 | buffer[5];

			current_glove->yaw_compensated = current_glove->yaw_offset - current_glove->yaw_raw;
			current_glove->pitch_compensated = current_glove->pitch_offset - current_glove->pitch_raw;
			current_glove->roll_compensated = current_glove->roll_offset - current_glove->roll_raw;

			//center data on 18000, meaning 180.00 degrees
			current_glove->yaw_compensated = (current_glove->yaw_compensated + 2 * 36000 + 18000) % 36000;
			current_glove->pitch_compensated = (current_glove->pitch_compensated + 2 * 36000 + 18000) % 36000;
			current_glove->roll_compensated = (current_glove->roll_compensated + 2 * 36000 + 18000) % 36000;

			current_glove->aaRealX = buffer[6] << 8 | buffer[7];
			current_glove->aaRealY = buffer[8] << 8 | buffer[9];
			current_glove->aaRealZ = buffer[10] << 8 | buffer[11];

			current_glove->gravityX = buffer[12] << 8 | buffer[13];
			current_glove->gravityY = buffer[14] << 8 | buffer[15];
			current_glove->gravityZ = buffer[16] << 8 | buffer[17];

			current_glove->color_sensor_R = buffer[18] << 8 | buffer[19];
			current_glove->color_sensor_G = buffer[20] << 8 | buffer[21];
			current_glove->color_sensor_B = buffer[22] << 8 | buffer[23];
			current_glove->color_sensor_Clear = buffer[24] << 8 | buffer[25];

			current_glove->finger1 = bitRead(buffer[26], 0);
			current_glove->finger2 = bitRead(buffer[26], 1);
			current_glove->finger3 = bitRead(buffer[26], 2);
			current_glove->finger4 = bitRead(buffer[26], 3);

			current_glove->packets_in_per_second = buffer[27];
			current_glove->packets_out_per_second = buffer[28];

			current_glove->framing_errors = buffer[29];
			current_glove->crc_errors = buffer[30];

			current_glove->cpu_usage = buffer[31];
			current_glove->cpu_temp = buffer[32];



			//update the gesture grid (8x8 grid + overlap 

			int temp_gloveX = 0;

			if (current_glove == &glove1){

				temp_gloveX = map((current_glove->yaw_compensated - 18000) - ((current_glove->pitch_compensated - 18000)*abs(sin((current_glove->roll_raw - 18000)* PI / 18000))) + 18000, 18000 - GLOVE_DEADZONE, 18000 + GLOVE_DEADZONE, 0, 8);
			}
			else{
				temp_gloveX = map((current_glove->yaw_compensated - 18000) + ((current_glove->pitch_compensated - 18000)*abs(sin((current_glove->roll_raw - 18000)* PI / 18000))) + 18000, 18000 - GLOVE_DEADZONE, 18000 + GLOVE_DEADZONE, 0, 8);
			}

			int temp_gloveY = map((current_glove->pitch_compensated), 18000 - GLOVE_DEADZONE, 18000 + GLOVE_DEADZONE, 0, 8);

			temp_gloveY = constrain(temp_gloveY, 0, 7);

			if (current_glove == &glove0){
				temp_gloveX = constrain(temp_gloveX, -8, 7);
				//only update position if not at the edge
				if (temp_gloveX > -8 && temp_gloveX < 7){
					glove0.gloveY = temp_gloveY;
				}
			}

			if (current_glove == &glove1){
				temp_gloveX = constrain(temp_gloveX, 0, 15);

				//only update position if not at the edge
				if (temp_gloveX > 0 && temp_gloveX < 15){
					glove1.gloveY = temp_gloveY;
				}
			}

			if (temp_gloveY>0 && temp_gloveY < 7){
				current_glove->gloveX = temp_gloveX;
			}

			//check for gestures
			readglove(&glove0);
			readglove(&glove1);

		}
	}
}

uint32_t wheel(uint16_t h, uint8_t s, uint8_t v){

	//h goes from 0 to 256*3-1 = 767
	//v goes from 0 to 255


	uint32_t r, g, b;

	switch (h / 256)
	{
	case 0:
		r = (255 - h % 256);   //Red down
		g = (h % 256);      // Green up
		b = 0;                  //blue off
		break;
	case 1:
		g = (255 - h % 256);  //green down
		b = (h % 256);      //blue up
		r = 0;                  //red off
		break;
	case 2:
		b = (255 - h % 256);  //blue down 
		r = (h % 256);      //red up
		g = 0;                  //green off
		break;
	}

	float brightness = (((float)v) / 255);
	r = r * brightness;
	g = g * brightness;
	b = b * brightness;

	return((gammatable[r] << 16) | (gammatable[g] << 8) | gammatable[b]);
}