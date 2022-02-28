/**
   @file water_sensor_lora_v3.ino
   @author olivia liebler, rakwireless.com
   @brief This sketch gets water sensing data from a "MH" Arduino water sensor and sends it on LoRa. Adapted from RAKWireless LoRa setup example, and IO 5811 example.
   @version 0.1
   @date 2021-10-30
   @copyright Copyright (c) 2021
**/
// includes for lora
#include <Arduino.h>
#include <LoRaWan-RAK4630.h> //http://librarymanager/All#SX126x
#include <SPI.h> 

// includes for water sensor IO
#ifdef _VARIANT_RAK4630_
#include <Adafruit_TinyUSB.h>
#endif

#define NO_OF_SAMPLES 32 // number of samples to include when calculating average value of voltage

// RAK4630 supply two LED - define LEDs included on the RAK4630 board
#ifndef LED_BUILTIN
#define LED_BUILTIN 35
#endif

#ifndef LED_BUILTIN2
#define LED_BUILTIN2 36
#endif

// lora definitions. CHANGE YOUR REGION (g_CurrentRegion) ACCORDINGLY (probably US915, but check!!)
bool doOTAA = true;   // OTAA is used by default. The alternative, ABP, is not defined in this program
#define SCHED_MAX_EVENT_DATA_SIZE APP_TIMER_SCHED_EVENT_DATA_SIZE /**< Maximum size of scheduler events. */
#define SCHED_QUEUE_SIZE 60                      /**< Maximum number of events in the scheduler queue. */
#define LORAWAN_DATERATE DR_0                   /*LoRaMac datarates definition, from DR_0 to DR_5*/
#define LORAWAN_TX_POWER TX_POWER_5             /*LoRaMac tx power definition, from TX_POWER_0 to TX_POWER_15*/
#define JOINREQ_NBTRIALS 3                      /**< Number of trials for the join request. */
DeviceClass_t g_CurrentClass = CLASS_A;         /* class definition*/
LoRaMacRegion_t g_CurrentRegion = LORAMAC_REGION_US915;    /* Region:US915*/
lmh_confirm g_CurrentConfirm = LMH_CONFIRMED_MSG;         /* confirm/unconfirm packet definition*/
uint8_t gAppPort = LORAWAN_APP_PORT;                      /* data port*/

/**@brief Structure containing LoRaWan parameters, needed for lmh_init()
*/
static lmh_param_t g_lora_param_init = {LORAWAN_ADR_ON, LORAWAN_DATERATE, LORAWAN_PUBLIC_NETWORK, JOINREQ_NBTRIALS, LORAWAN_TX_POWER, LORAWAN_DUTYCYCLE_OFF};

// Foward declaration
static void lorawan_has_joined_handler(void);
static void lorawan_join_failed_handler(void);
static void lorawan_rx_handler(lmh_app_data_t *app_data);
static void lorawan_confirm_class_handler(DeviceClass_t Class);
static void send_lora_frame(void);

/**@brief Structure containing LoRaWan callback functions, needed for lmh_init()
*/
static lmh_callback_t g_lora_callbacks = {BoardGetBatteryLevel, BoardGetUniqueId, BoardGetRandomSeed,
                                        lorawan_rx_handler, lorawan_has_joined_handler, lorawan_confirm_class_handler, lorawan_join_failed_handler
                                       };

//OTAA keys !!!! KEYS ARE MSB !!!! Used for Join requests to the LoRaWAN Gateway. Update these to the values generated by Helium!
uint8_t nodeDeviceEUI[8] = {0x60, 0x81, 0xF9, 0x7F, 0xE5, 0x11, 0x0C, 0xF6};
uint8_t nodeAppEUI[8] = {0x60, 0x81, 0xF9, 0xD9, 0x2F, 0x7B, 0xB5, 0x30};
uint8_t nodeAppKey[16] = {0xF7, 0x9F, 0x5C, 0x52, 0x86, 0x3A, 0x3F, 0xA2, 
  0x0C, 0x7D, 0x10, 0xDC, 0xAB, 0x30, 0x5C, 0x91};

// Private definition. SET your INTERVAL with LORAWAN_APP_INTERVAL.
#define LORAWAN_APP_DATA_BUFF_SIZE 64                     /**< buffer size of the data to be transmitted, in bytes. */
#define LORAWAN_APP_INTERVAL 60000                        /**< Defines for user timer, the application data transmission interval. 60s, value in [ms]. */
static uint8_t m_lora_app_data_buffer[LORAWAN_APP_DATA_BUFF_SIZE];            //< Lora user application data buffer. This will hold the payload.
static lmh_app_data_t m_lora_app_data = {m_lora_app_data_buffer, 0, 0, 0, 0}; //< Lora user application data structure.

// timer, keeps track of time to regulate time intervals on the device
TimerEvent_t appTimer;
static uint32_t timers_init(void);
static uint32_t count = 0;       // represents the number of packets sent successfully since program started
static uint32_t count_fail = 0;  // represents the number of packets that failed to send since program started

// global variable that gets set to the average value on the liquid/sap sensor
int sensor_value = 0;

void setup() {
  // put your setup code here, to run once:

  // start of lora setup. 

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize LoRa chip.
  lora_rak4630_init();

  // Initialize Serial for debug output
  time_t timeout = millis();
  Serial.begin(115200);   // argument is baud rate, or rate of symbols per second sent to the Serial Monitor
  while (!Serial)
  {
    if ((millis() - timeout) < 5000)
    {
      delay(100);
    }
    else
    {
      break;
    }
  }
  Serial.println("=====================================");
  Serial.println("Welcome to RAK4630 LoRaWan!!!");
  if (doOTAA)
  {
    Serial.println("Type: OTAA");
  }

  switch (g_CurrentRegion) // prints the region that the device is set to
  {
    case LORAMAC_REGION_AS923:
      Serial.println("Region: AS923");
      break;
    case LORAMAC_REGION_AU915:
      Serial.println("Region: AU915");
      break;
    case LORAMAC_REGION_CN470:
      Serial.println("Region: CN470");
      break;
    case LORAMAC_REGION_EU433:
      Serial.println("Region: EU433");
      break;
    case LORAMAC_REGION_IN865:
      Serial.println("Region: IN865");
      break;
    case LORAMAC_REGION_EU868:
      Serial.println("Region: EU868");
      break;
    case LORAMAC_REGION_KR920:
      Serial.println("Region: KR920");
      break;
    case LORAMAC_REGION_US915:
      Serial.println("Region: US915");
      break;
  }
  Serial.println("=====================================");
  
  //create a user timer to send data to server according to the specified period. 
  /**
   * Calling timers_init() will call tx_lora_periodic_handler(), which calls send_lora_frame, 
   * which calls library function lmh_send() to send the packet to the gateway.
  */
  uint32_t err_code;
  err_code = timers_init();
  if (err_code != 0)
  {
    Serial.printf("timers_init failed - %d\n", err_code);
    return;
  }

  // Setup the EUIs and Keys
  if (doOTAA)
  {
    lmh_setDevEui(nodeDeviceEUI);
    lmh_setAppEui(nodeAppEUI);
    lmh_setAppKey(nodeAppKey);
  }


  // Initialize LoRaWan
  err_code = lmh_init(&g_lora_callbacks, g_lora_param_init, doOTAA, g_CurrentClass, g_CurrentRegion);
  if (err_code != 0)
  {
    Serial.printf("lmh_init failed - %d\n", err_code);
    return;
  }

  // Start Join procedure
  lmh_join();
  
  // end of lora setup.



  // start IO sensor setup.

  /* WisBLOCK 5811 Power On*/
    pinMode(WB_IO1, OUTPUT);
    digitalWrite(WB_IO1, HIGH);
  /* WisBLOCK 5811 Power On*/
  
    pinMode(WB_A1, INPUT_PULLDOWN);
    analogReference(AR_INTERNAL_3_0);
    analogOversampling(128);

  // end of IO sensor setup.

}

void loop() {
  // put your main code here, to run repeatedly:

  int i;
  int mcu_ain_raw = 0; 
  int analog_read; 
  int min_sampled_raw = 1023;          // minimum value read off the sensor out of 32 samples
  int max_sampled_raw = 0;          // maximum value read off the sensor out of 32 samples
  int average_raw;
  float mcu_ain_voltage;
  float voltage_sensor;               // variable to store the voltage value coming from the sensor

  for (i = 0; i < NO_OF_SAMPLES; i++)
  {
    analog_read = analogRead(WB_A1); // the input pin A1 for the potentiometer
    mcu_ain_raw += analog_read;       
    
    if (analog_read < min_sampled_raw) min_sampled_raw = analog_read;
    if (analog_read > max_sampled_raw) max_sampled_raw = analog_read;
    
  }
  average_raw = mcu_ain_raw / i;
  sensor_value = average_raw;
  
  mcu_ain_voltage = average_raw * 3.0 / 1024;   //raef 3.0V / 10bit ADC

  voltage_sensor = mcu_ain_voltage / 0.6;     //WisBlock RAK5811 (0 ~ 5V).   Input signal reduced to 6/10 and output
  
  Serial.printf("-------average_value------ = %d\n", average_raw);
  Serial.printf("-------minimum------------ = %d\n", min_sampled_raw);
  Serial.printf("-------maximum------------ = %d\n", max_sampled_raw);
  Serial.printf("-------voltage_sensor------ = %f\n", voltage_sensor);

  if (average_raw > 600) {
    if (average_raw >= 1023) {
      Serial.printf("Read error - Please dry off sensor. \n");
    }
    else {
      if (average_raw < 635) Serial.printf("Bucket is full \n");
    else Serial.printf("Bucket will soon overflow \n");
    }
  }

  // delay the next measurement for 20 seconds  
  delay(20000);

}


// other LORA functions

/**@brief LoRa function for handling HasJoined event.
 */
void lorawan_has_joined_handler(void)
{
  Serial.println("OTAA Mode, Network Joined!");

  lmh_error_status ret = lmh_class_request(g_CurrentClass);
  if (ret == LMH_SUCCESS)
  {
    delay(1000);
    TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
    TimerStart(&appTimer);
  }
}
/**@brief LoRa function for handling OTAA join failed
*/
static void lorawan_join_failed_handler(void)
{
  Serial.println("OTAA join failed!");
  Serial.println("Check your EUI's and Keys's!");
  Serial.println("Check if a Gateway is in range!");
}
/**@brief Function for handling LoRaWan received data from Gateway
 *
 * @param[in] app_data  Pointer to rx data
 */
void lorawan_rx_handler(lmh_app_data_t *app_data)
{
  Serial.printf("LoRa Packet received on port %d, size:%d, rssi:%d, snr:%d, data:%s\n",
          app_data->port, app_data->buffsize, app_data->rssi, app_data->snr, app_data->buffer);
}

void lorawan_confirm_class_handler(DeviceClass_t Class)
{
  Serial.printf("switch to class %c done\n", "ABC"[Class]);
  // Informs the server that switch has occurred ASAP
  m_lora_app_data.buffsize = 0;
  m_lora_app_data.port = gAppPort;
  lmh_send(&m_lora_app_data, g_CurrentConfirm);
}

void send_lora_frame(void)
{
  if (lmh_join_status_get() != LMH_SET)
  {
    //Not joined, try again later
    return;
  }

  uint32_t i = 0;
  memset(m_lora_app_data.buffer, 0, LORAWAN_APP_DATA_BUFF_SIZE);
  m_lora_app_data.port = gAppPort;
  m_lora_app_data.buffer[i++] = (sensor_value >> 8);
  m_lora_app_data.buffer[i++] = sensor_value; 
  m_lora_app_data.buffsize = i;

  lmh_error_status error = lmh_send(&m_lora_app_data, g_CurrentConfirm);
  if (error == LMH_SUCCESS)
  {
    count++;
    Serial.printf("lmh_send ok count %d\n", count);
  }
  else
  {
    count_fail++;
    Serial.printf("lmh_send fail count %d\n", count_fail);
  }
}

/**@brief Function for handling user timerout event.
 */
void tx_lora_periodic_handler(void)
{
  TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
  TimerStart(&appTimer);
  Serial.println("Sending frame now...");
  send_lora_frame();
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
uint32_t timers_init(void)
{
  TimerInit(&appTimer, tx_lora_periodic_handler);
  return 0;
}

// end other LORA functions
