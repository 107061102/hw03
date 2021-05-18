#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"
// RPC
#include "mbed_rpc.h"
#include "mbed.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "uLCD_4DGL.h"
#include "math.h"
#include "stm32l475e_iot01_accelero.h"

using namespace std::chrono;

#define PI 3.14159265
// Create an area of memory to use for input, output, and intermediate arrays.
// The size of this will depend on the model you're using, and may need to be
// determined by experimentation.
DigitalOut myled1(LED1);
DigitalOut myled2(LED2);
DigitalOut myled3(LED3);
InterruptIn mypin(USER_BUTTON);
BufferedSerial pc(USBTX, USBRX);
uLCD_4DGL uLCD(D1, D0, D2);

void GestureUI(Arguments *in, Reply *out);
RPCFunction rpcLED(&GestureUI, "GestureUI");
void Tilt(Arguments *in, Reply *out);
RPCFunction jud(&Tilt, "TILT");
void flip(Arguments *in, Reply *out);
RPCFunction change(&flip, "FLIP");
int Gestureselect();
void Tiltjudge();


constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

EventQueue queue(32 * EVENTS_EVENT_SIZE);
EventQueue queue2(32 * EVENTS_EVENT_SIZE);
EventQueue time_queue(32 * EVENTS_EVENT_SIZE);
EventQueue mqtt_queue(32 * EVENTS_EVENT_SIZE);

Thread t;
Thread t2;
Thread t3;
Thread mqtt_thread(osPriorityHigh);

WiFiInterface *wifi;
double x, y;
int angle = 30;
double true_angle = 0;
int mode = 0;
MQTT::Client<MQTTNetwork, Countdown> *client2;
int16_t value[3];
char buf[256], outbuf[256];
double thres, val;

FILE *devin = fdopen(&pc, "r");
FILE *devout = fdopen(&pc, "w");

int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;

const char* topic = "Mbed";




void flip(Arguments *in, Reply *out) {
  mode = !mode;
  //printf("%d", mode);
}

void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) {
    if (mode == 0 || message_num == 10) {
      message_num = 0;
    } else {
      message_num++;
    }
    if (mode == 0) {
      true_angle = angle;
    }
    MQTT::Message message;
    char buff[100];
    sprintf(buff, "%d %d %f", mode, message_num, true_angle);
    
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buff;
    message.payloadlen = strlen(buff) + 1;
    int rc = client->publish(topic, message);

}

void close_mqtt() {
    closed = true;
}


int main() {
    //The mbed RPC classes are now wrapped to create an RPC enabled version - see RpcClasses.h so don't add to base class

    // receive commands, and send back the responses
    BSP_ACCELERO_Init();
    wifi = WiFiInterface::get_default_instance();
    if (!wifi) {
            printf("ERROR: No WiFiInterface found.\r\n");
            return -1;
    }


    printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
            printf("\nConnection error: %d\r\n", ret);
            return -1;
    }


    NetworkInterface* net = wifi;
    MQTTNetwork mqttNetwork(net);
    MQTT::Client<MQTTNetwork, Countdown>client(mqttNetwork);
    client2 = &client;
    //TODO: revise host to your IP
    const char* host = "192.168.246.234";
    printf("Connecting to TCP network...\r\n");

    SocketAddress sockAddr;
    sockAddr.set_ip_address(host);
    sockAddr.set_port(1883);

    printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

    int rc = mqttNetwork.connect(sockAddr);//(host, 1883);
    if (rc != 0) {
            printf("Connection error.");
            return -1;
    }
    printf("Successfully connected!\r\n");

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = "Mbed";

    if ((rc = client.connect(data)) != 0){
            printf("Fail to connect MQTT\r\n");
    }
    if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
            printf("Fail to subscribe\r\n");
    }

    mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
    mypin.rise(mqtt_queue.event(&publish_message, &client));
    int num = 0;
    
    while (num != 5) {
            client.yield(100);
            ++num;
    }
    

    while(1) {
        
        memset(buf, 0, 256);
        for (int i = 0; ; i++) {
            char recv = fgetc(devin);
            if (recv == '\n') {
                printf("\r\n");
                break;
            }
            buf[i] = fputc(recv, devout);
        }
        //Call the static call method on the RPC class
        RPC::call(buf, outbuf);
        //printf("testsuccess\r\n");
    }
}


// Make sure the method takes in Arguments and Reply objects.
void GestureUI (Arguments *in, Reply *out)   {
    bool success = true;

    // In this scenario, when using RPC delimit the two arguments with a space.
    mode = 0;
    // Have code here to call another RPC function to wake up specific led or close it.
    char buffer[200], outbuf[256];
    char strings[20];
    t.start(callback(&queue, &EventQueue::dispatch_forever));
    queue.call(Gestureselect);
}



// Return the result of the last prediction
int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;

  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;
  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }

  // No gesture was detected above the threshold
  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result
  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}

int Gestureselect() {
  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;

  // The gesture index of the prediction
  int gesture_index;

  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return -1;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();

  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    return -1;
  }

  int input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    return -1;
  }

  error_reporter->Report("Set up successful...\n");
  myled1 = 1;
  while (true) {
    
    // Attempt to read new data from the accelerometer
    got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                 input_length, should_clear_buffer);

    // If there was no new data,
    // don't try to clear the buffer again and wait until next time
    if (!got_data) {
      should_clear_buffer = false;
      continue;
    }

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      error_reporter->Report("Invoke failed on index: %d\n", begin_index);
      continue;
    }

    // Analyze the results to obtain a prediction
    gesture_index = PredictGesture(interpreter->output(0)->data.f);

    // Clear the buffer next time we read data
    should_clear_buffer = gesture_index < label_num;

    // Produce an output
    if (gesture_index == 0){
      angle += 5;
      uLCD.color(BLUE);  
      uLCD.background_color(WHITE);
      uLCD.cls();
      uLCD.textbackground_color(WHITE);
      // basic printf demo = 16 by 18 characters on screen
      uLCD.locate(1, 1);
      uLCD.text_width(4); //4X size text
      uLCD.text_height(4);
      uLCD.color(GREEN);
      uLCD.printf("\n%d\n", angle);
    }
    if (mode == 1){
      myled1 = 0;
      return 0;
    }
    if (gesture_index == 2){
      angle -= 5;
      uLCD.color(BLUE);  
      uLCD.background_color(WHITE);
      uLCD.cls();
      uLCD.textbackground_color(WHITE);
      // basic printf demo = 16 by 18 characters on screen
      uLCD.locate(1, 1);
      uLCD.text_width(4); //4X size text
      uLCD.text_height(4);
      uLCD.color(GREEN);
      uLCD.printf("\n%d\n", angle);
    }
  }
}


void Tilt (Arguments *in, Reply *out)   {
    bool success = true;
    myled2 = 1;
    mode = 1;
    // Have code here to call another RPC function to wake up specific led or close it.
    char buffer[200], outbuf[256];
    char strings[20];
    while (1) {
      BSP_ACCELERO_AccGetXYZ(value);
      if (value[2] > 980){
        myled2 = 0;
        break;
      }
    }
    t2.start(callback(&queue2, &EventQueue::dispatch_forever));
    queue2.call(Tiltjudge);
}
void Tiltjudge() {
    myled3 = 1;
    t3.start(callback(&time_queue, &EventQueue::dispatch_forever));
    

    while (1){
      BSP_ACCELERO_AccGetXYZ(value);
      true_angle=acos(value[2]/1000.0)*180/PI;
      uLCD.cls();
      uLCD.color(BLUE);  
      uLCD.background_color(WHITE);
      uLCD.textbackground_color(WHITE);
      // basic printf demo = 16 by 18 characters on screen
      uLCD.locate(1, 1);
      uLCD.text_width(4); //4X size text
      uLCD.text_height(4);
      uLCD.color(GREEN);
      uLCD.printf("\n%.2lf\n", true_angle);

      
      if(value[2]<cos(PI/180*angle)*1000 && mode == 1){
        time_queue.call(&publish_message, client2);
      }

      if (mode == 0){
        myled3 = 0;
        return;
      }
      ThisThread::sleep_for(500ms);
    }
}
