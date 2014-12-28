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

typedef struct {
	uint8_t color_sensor_R;
	uint8_t color_sensor_G;
	uint8_t color_sensor_B;
} SINGLELED;




typedef struct {
	boolean fresh;
	boolean in_progress;
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

byte gammatable[256];


const int ledsPerStrip = 128;

DMAMEM int displayMemory[ledsPerStrip * 6];
int drawingMemory[ledsPerStrip * 6];

const int config = WS2811_GRB | WS2811_800kHz;

uint8_t helmet_LED_R = 0xff;
uint8_t helmet_LED_G =0;
uint8_t helmet_LED_B =0;

#define GLOVE_DEADZONE 3000  //30 degrees
#define GLOVE_MAXIMUM 30000 //90 degrees


#define OLED_DC     17
#define OLED_CS     22
#define OLED_RESET  19
Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);


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

GLOVE glove0;
GLOVE glove1;
SERIALSTATS serial1stats;

boolean gloveindicator[16][8];

Metro FPSdisplay = Metro(1000);

Metro YPRdisplay = Metro(100);

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

	glove1.in_progress = false;
	glove0.in_progress = false;
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

	AudioMemory(5);
	fft256_1.windowFunction(AudioWindowHanning256);
	fft256_1.averageTogether(4);

	leds.show();
}

long int magictime = 0;
int ledmodifier = 1;
int indexled = 0;

void loop() {

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



	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 8; y++) {
			gloveindicator[x][y] = false;
		}
	}

	gloveindicator[8 + glove0.gloveX][glove0.gloveY] = true;
	gloveindicator[glove1.gloveX][glove1.gloveY] = true;



	display.clearDisplay();
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 8; y++) {
			if (gloveindicator[x][y] == true){
				display.drawPixel(x, y, WHITE);
			}
		}
	}

	display.display();

	//erase array
	for (int i = 0; i < leds.numPixels(); i++) {
		leds.setPixel(i, 0x00000000);
	}

	uint32_t tempcolor = (uint32_t)gammatable[helmet_LED_R] << 16 | (uint32_t)gammatable[helmet_LED_G] << 8 | (uint32_t)gammatable[helmet_LED_B];
	int index_led = 0;
	for (int y = 0; y < 8; y++) {
		if (y % 2){ // for odd rows scan from right side
			for (int x = 15; x > -1; x--) {
				if (gloveindicator[x][y] == true){
					leds.setPixel(index_led + 15-x, tempcolor);
				}
			}
			index_led = index_led + 16;
		}
		else{  // for even rows scan from left side
			for (int x = 0; x < 16; x++) {
				if (gloveindicator[x][y] == true){
					leds.setPixel(index_led, tempcolor);
				}
				index_led++;
			}

		}
	}
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

	if (YPRdisplay.check() ){
		long int start = micros();

		display.reinit();
	
		

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



	if (FPSdisplay.check()){



		//cpu_usage = 100 - (idle_microseconds / 10000);
		//local_packets_out_per_second_1 = local_packets_out_counter_1;
		//local_packets_in_per_second_1 = local_packets_in_counter_1;
		//idle_microseconds = 0;
		// local_packets_out_counter_1 = 0;
		// local_packets_in_counter_1 = 0;
	

		

	}

	float n;
	int i;

	if (fft256_1.available() &&0) {
		// each time new FFT data is available
		// print it all to the Arduino Serial Monitor
		Serial.print("FFT: ");
		for (i = 0; i < 40; i++) {
		n = fft256_1.read(i);
		if (n >= 0.01) {
			Serial.print(n);
			Serial.print(" ");
			}
			else {
				Serial.print("  -  "); // don't print "0.00"
			}
			}
			Serial.println();
	}

	SerialUpdate();
}

void readglove(void * temp){





	GLOVE * current_glove = (GLOVE *)temp;
	if (current_glove->finger1 == 1 && current_glove->in_progress == true){

		if (current_glove->gloveY <= 0){
				Serial.println(" down!");
		}
		else
		{
			if (current_glove->gloveY >= 7){
				Serial.println(" up!");
			}

			else{
				if (current_glove->gloveX <= 0){
					Serial.println(" right!");
				}

				if (current_glove->gloveX >= 7){
					Serial.println(" left!");
				}
			}
		}
	}

	if (current_glove->finger1 == 0){
		if (current_glove->in_progress == false){
			current_glove->in_progress = true;
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
		current_glove->in_progress = false;
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
			current_glove->yaw_compensated = (current_glove->yaw_compensated + 2*36000 +18000 ) % 36000;
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
				temp_gloveX = constrain(temp_gloveX, 0,15);

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

