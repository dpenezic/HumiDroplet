#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BMP280.h"

// Firmware Version
const String Ver = "Firmware v1.2.6";

// Sensors
#define dhtPin D3
#define CheckPin D5
#define BMPAddress 0x76
#define AnalogPin A0
DHT dht(dhtPin, DHT22);
Adafruit_BMP280 BMP; //I2C

					 // ThingSpeak
const String APIKey = "Your API Key";
const char* DataHost = "api.ThingSpeak.com";
const unsigned long MyChannelNumber = /*Your channel number*/;
const int HTTPPort = 80;

// WiFi
ESP8266WiFiMulti wifiMulti;
boolean ConnectionWasAlive = true;
boolean LostWiFiConnection = false;
WiFiServer Server(80);
WiFiClient client;
String LocalIP;

// Timers [ms]
unsigned long LastThingSpeakUpdate = 0;
unsigned long LastCycleTick = 0;
const unsigned int CycleInterval = 20000; // Change this value however you like, this is the local refresh interval.
const unsigned int ThingSpeakUpdateInterval = 20000; // 15000ms minimum, otherwise ThingSpeak won't accept some of the updates.

struct Packet
{
	double Humidity;
	double Temperature;
	double Pressure;
	double Luminance;
	double DewPoint;
	int8_t RSSIdBm;
	uint8_t RSSIPercent;
};

Packet Fresh;

// Average calculation variables
const uint8_t AverageAmount = 30;
Packet Average;
Packet AverageArray[AverageAmount];
uint8_t AverageCounter = 0;
bool AverageFlag = false;

//Functions
void MonitorWiFi();
double Lux(int ADC, int bit, double Vin, double R2);
double DewPointCalc(double T, double RH);
void UpdateReadings(Packet &Output);
void DisplayPacket(const Packet & Input);
void ThingSpeakUpdate(const Packet & Input);
void HTTP(String HTML);
String MergeHTML(const Packet & Input);
void CalcAverage(const struct Packet InputArray[], Packet &Output, const double &Amount, uint8_t &Counter, bool &Flag);
void UpdateAverageArray(const Packet &Input, struct Packet OutputArray[], const double &Amount, uint8_t &Counter, bool &Flag);
bool Cycle(unsigned long &Time, const unsigned int &Interval);
int dBmToPercent(const int RSSI);

unsigned long DebugCounter = 0;

void setup()
{
	Serial.begin(115200);
	for (int x = 0; x < 8; x++) { Serial.print("  \n"); }

	//Wire.begin();
	BMP.begin(BMPAddress);
	dht.begin();
	pinMode(dhtPin, INPUT_PULLUP);
	pinMode(CheckPin, INPUT_PULLUP);

	wifiMulti.addAP("SSID1", "Passphrase1");
	wifiMulti.addAP("SSID2", "Passphrase2");

	Serial.println("NodeMCU Weather Station\n" + Ver);

	while (wifiMulti.run() != WL_CONNECTED)
	{
		MonitorWiFi();
	}

	Server.begin();
	Serial.println("Web server running.");
}

void loop()
{
	MonitorWiFi();
	HTTP(MergeHTML(Average));
	if (Cycle(LastCycleTick, CycleInterval))
	{
		Serial.println();
		Serial.println(DebugCounter);
		DebugCounter++;

		UpdateReadings(Fresh);
		UpdateAverageArray(Fresh, AverageArray, AverageAmount, AverageCounter, AverageFlag);
		CalcAverage(AverageArray, Average, AverageAmount, AverageCounter, AverageFlag);
		Serial.print("Fresh");
		DisplayPacket(Fresh);
		Serial.print("\nAverage");
		DisplayPacket(Average);
	}
	ThingSpeakUpdate(Fresh);
}

void MonitorWiFi()
{
	while (wifiMulti.run() != WL_CONNECTED)
	{
		if (ConnectionWasAlive == true)
		{
			if (LostWiFiConnection == true)
			{
				LostWiFiConnection = false;
				Serial.println("WiFi connection lost!");
			}
			ConnectionWasAlive = false;
			Serial.print("Scanning WiFi networks");
		}
		Serial.print('.');
		delay(250);
	}
	if (ConnectionWasAlive == false)
	{
		ConnectionWasAlive = true;
		LostWiFiConnection = true;
		Serial.println("Successfully connected to " + WiFi.SSID());
		LocalIP = String(WiFi.localIP().toString());
		Serial.println("Local IP is: " + LocalIP);
	}
}

void HTTP(String HTML)
{
	WiFiClient client = Server.available();

	if (client)
	{
		Serial.println("New client connected!");
		boolean blankLine = true;

		while (client.connected())
		{
			if (client.available())
			{
				char temp = client.read();

				if (temp == '\n' && blankLine == true)
				{
					//// WEBSITE HTML CODE ////
					client.println(HTML);
					//client.println("TEST1234");
					///////////////////////////
					break;
				}
				if (temp == '\n')
				{
					blankLine = true;
				}
				else if (temp != '\r')
				{
					blankLine = false;
				}
			}
		}
		delay(1);
		client.stop();
		Serial.println("Client disconnected.");
	}
}

void UpdateReadings(Packet &Output)
{
	//DHT22
	Output.Humidity = dht.readHumidity(); // Relative Humidity [%]
										  //BMP280
	Output.Temperature = BMP.readTemperature();// dht.readTemperature(); // [*C]
	Output.Pressure = BMP.readPressure() / 100; // Pa / 100 = [hPa]
	Output.Luminance = Lux(analogRead(AnalogPin), 10, 3.3, 10); // lumens [lm]
																//RSSI
	Output.RSSIdBm = WiFi.RSSI(); // [dBm]
	Output.RSSIPercent = dBmToPercent(Output.RSSIdBm); // [%]
													   //Dew point
	Output.DewPoint = DewPointCalc(Output.Temperature, Output.Humidity); // [*C]
}

void DisplayPacket(const Packet & Input)
{
	Serial.print("\r\n");

	Serial.print("Temperature: ");
	Serial.print(Input.Temperature);
	Serial.println("*C");

	Serial.print("Humidity: ");
	Serial.print(Input.Humidity);
	Serial.println("%");

	Serial.print("DewPoint: ");
	Serial.print(Input.DewPoint);
	Serial.println("*C");

	Serial.print("Pressure: ");
	Serial.print(Input.Pressure);
	Serial.println(" hPa");

	Serial.print("Luminance: ");
	Serial.print(Input.Luminance);
	Serial.println(" lm");

	Serial.print("RSSIdBm: ");
	Serial.print(Input.RSSIdBm);
	Serial.println(" dBm");

	Serial.print("RSSIPercent: ");
	Serial.print(Input.RSSIPercent);
	Serial.println("%");

	Serial.print("LocalIP: ");
	Serial.print(LocalIP);
	Serial.println();
}

void ThingSpeakUpdate(const Packet & Input)
{
	if (Cycle(LastThingSpeakUpdate, ThingSpeakUpdateInterval) && (digitalRead(CheckPin) == LOW))
	{
		Serial.println("\r\nCheckPin State: " + String(digitalRead(CheckPin)));
		//LastThingSpeakUpdate = millis();

		// Converting readings to strings
		String Temperature, Humidity, Pressure, Luminance, RSSIdBm, RSSIPercent, Status, DewPoint;
		Temperature = String(Input.Temperature);	// field1
		Humidity = String(Input.Humidity);			// field2
		Pressure = String(Input.Pressure);			// field3
		Luminance = String(Input.Luminance);		// field4
		RSSIdBm = String(Input.RSSIdBm);			// field5
		RSSIPercent = String(Input.RSSIPercent);	// field6
		DewPoint = String(Input.DewPoint);			// field7
		Status = Ver + " | " + "Local IP: " + LocalIP;		// status

															// Creating string with data to send
		String Data = "/update?key=";
		Data += APIKey;
		Data += "&field1=" + Temperature;
		Data += "&field2=" + Humidity;
		Data += "&field3=" + Pressure;
		Data += "&field4=" + Luminance;
		Data += "&field5=" + RSSIdBm;
		Data += "&field6=" + RSSIPercent;
		Data += "&field7=" + DewPoint;
		Data += "&status=" + Status;

		Serial.println("Data string: " + Data);
		Serial.print("\r\nConnecting to ThingSpeak...");

		// Connecting and sending data to ThingSpeak
		if (client.connect(DataHost, HTTPPort))
		{
			client.print("POST /update HTTP/1.1\n");
			client.print("Host: api.ThingSpeak.com\n");
			client.print("Connection: close\n");
			client.print("X-ThingSpeakAPIKEY: " + APIKey + "\n");
			client.print("Content-Type: application/x-www-form-urlencoded\n");
			client.print("Content-Length: ");
			client.print(Data.length());
			client.print("\n\n");
			client.print(Data);

			delay(200);
			// ThingSpeak update sent successfully.
			Serial.println("ThingSpeak data update sent successfully.");
		}
		else {
			// Failed to connect to ThingSpeak
			Serial.println("Unable to connect to ThingSpeak.");
		}

		if (!client.connected()) {
			client.stop();
		}
		client.flush();
		client.stop();
	}
}

double Lux(int ADC, int bit, double Vin, double R2)
{
	//ADC - analogRead value
	//bit - ADC resolution
	//Vin - input voltage
	//R2 - Low side resistor

	double Vout = ADC * Vin / pow(2, bit);
	double Output = (500 * Vout) / (R2 * (Vin - Vout));

	//Serial.println(Vout); // Debugging purposes

	return Output;
}

String MergeHTML(const Packet & Input)
{
	String Code = "<html><body><h1>NodeMCU Weather Station</h1>";
	Code += "Local IP: " + LocalIP + "<br/><br/>";
	Code += "Humidity: " + String(Input.Humidity) + "%" + "<br/>";
	Code += "Temperature: " + String(Input.Temperature) + "*C" + "<br/>";
	Code += "Pressure: " + String(Input.Pressure) + " hPa" + "<br/>";
	Code += "Luminance: " + String(Input.Luminance) + " lm" + "<br/>";
	Code += "RSSIdBm: " + String(Input.RSSIdBm) + " dBm" + "<br/>";
	Code += "RSSIPercent: " + String(Input.RSSIPercent) + "%" + "<br/>";
	Code += "DewPoint: " + String(Input.DewPoint) + "*C" + "<br/><br/>";
	Code += Ver;
	Code += "</html></body>";

	return Code;
}

double DewPointCalc(double T, double RH)
{
	double B = (log(RH / 100) + ((17.27 * T) / (237.3 + T))) / 17.27;
	double D = (237.3 * B) / (1 - B);
	return D;
}

void CalcAverage(const struct Packet InputArray[], Packet& Output, const double &Amount, uint8_t &Counter, bool &Flag)
{
	if (AverageFlag)
	{
		memset(&Output, 0, sizeof(Packet));
		for (int x = 0; x < Amount; x++)
		{
			Output.Temperature += InputArray[x].Temperature / Amount;
			Output.Humidity += InputArray[x].Humidity / Amount;
			Output.Pressure += InputArray[x].Pressure / Amount;
			Output.Luminance += InputArray[x].Luminance / Amount;
			Output.DewPoint += InputArray[x].DewPoint / Amount;
			//Serial.println(String(x) + "Average RSSIdBm: " + String(InputArray[x].RSSIdBm));
			Output.RSSIdBm += InputArray[x].RSSIdBm / Amount;
			Output.RSSIPercent += InputArray[x].RSSIPercent / Amount;
		}
	}
}

void UpdateAverageArray(const Packet &Input, struct Packet OutputArray[], const double &Amount, uint8_t &Counter, bool &Flag)
{
	OutputArray[Counter] = Input;
	Counter++;
	if (Counter == Amount)
	{
		Counter = 0;
		Flag = true;
	}
}

bool Cycle(unsigned long &Time, const unsigned int &Interval)
{
	if (millis() - Time >= Interval)
	{
		Time = millis();
		return true;
	}
	return false;
}

int dBmToPercent(const int RSSI)
{
	if (RSSI < -90)
		return 0;

	return map(RSSI, -90, -30, 0, 100);
}