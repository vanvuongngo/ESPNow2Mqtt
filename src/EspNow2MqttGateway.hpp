#if !defined(_ESPNOW2MQTTGATEWAY_HPP_)
#define _ESPNOW2MQTTGATEWAY_HPP_

#include <Arduino.h>
#include "criptMsg.hpp"
#include <pb_decode.h>
#include <pb_encode.h>
#include "messages.pb.h"
#include <esp_now.h>
#include "EspNowUtil.hpp"
#include <PubSubClient.h>

void EspNow2Mqtt_onResponseSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void EspNow2Mqtt_onDataReceived(const uint8_t * mac_addr, const uint8_t *incomingData, int len);
// -- class definition ----------------------------------------------------------------------------
class EspNow2MqttGateway
{
private:
    static EspNow2MqttGateway *espNow2MqttGatewaySingleton;
    CriptMsg crmsg = CriptMsg();
    response gwResponse = response_init_zero;
    request decodedRequest = request_init_default;
    response emptyResponse = response_init_zero;
    request defaultRequest = request_init_default;
    EspNowUtil eNowUtil;
public:
    EspNow2MqttGateway(byte* key, int espnowChannel = 0);
    ~EspNow2MqttGateway();
    int init();
    static EspNow2MqttGateway* getSingleton() {return espNow2MqttGatewaySingleton;}
    void espNowHandler(const uint8_t * mac_addr, const uint8_t *incomingData, int len);
private:
    void pingHandler(const uint8_t * mac_addr, request_Ping & ping, response_OpResponse & rsp);
    void sendHandler(const uint8_t * mac_addr, request_Send & ping, response_OpResponse & rsp);
    void subscribeHandler(const uint8_t * mac_addr, request_Subscribe & ping, response_OpResponse & rsp);
    void buildResponse (response_Result code, char * payload , response_OpResponse & rsp);
    void deserializeRequest(request &rq, const uint8_t *incomingData, int len);
    int serializeResponse (u8_t * buffer, response &rsp);
    friend void EspNow2Mqtt_onResponseSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    friend void EspNow2Mqtt_onDataReceived(const uint8_t * mac_addr, const uint8_t *incomingData, int len);
public:
    std::function<void(bool ack, request&, response&)> onProcessedRequest = NULL;
    std::function<void(const uint8_t * mac_addr, const uint8_t *incomingData, int len)> onDataReceived = NULL;
};
EspNow2MqttGateway* EspNow2MqttGateway::espNow2MqttGatewaySingleton = nullptr;;

// -- friend functions ----------------------------------------------------------------------------
void EspNow2Mqtt_onDataReceived(const uint8_t * mac_addr, const uint8_t *incomingData, int len){
    EspNow2MqttGateway* instance = EspNow2MqttGateway::getSingleton();
    if (instance){
        if (instance->onDataReceived){
            instance->onDataReceived(mac_addr,incomingData, len);
        }
        instance->espNowHandler( mac_addr, incomingData, len);
    }
}

void EspNow2Mqtt_onResponseSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    EspNow2MqttGateway * instance = EspNow2MqttGateway::getSingleton();
    if(instance && instance->onProcessedRequest) {
        instance->onProcessedRequest(status == ESP_OK, instance->decodedRequest, instance->gwResponse);    
    }
}

// -- class implementation ------------------------------------------------------------------------
EspNow2MqttGateway::EspNow2MqttGateway(byte* key, int espnowChannel):
eNowUtil(espnowChannel)
{
    std::copy(key, key+crmsg.keySize, crmsg.key);
    espNow2MqttGatewaySingleton = this;
}

EspNow2MqttGateway::~EspNow2MqttGateway()
{
}

int EspNow2MqttGateway::init()
{
    esp_now_register_send_cb(EspNow2Mqtt_onResponseSent);

    Serial.println("registration ok");
    //init esp-now, gw will be registered as a handler for incoming messages
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
    }
    return 0;
}

void EspNow2MqttGateway::espNowHandler(const uint8_t * mac_addr, const uint8_t *incomingData, int len)
{
    //create request object
    decodedRequest = defaultRequest;
    deserializeRequest(decodedRequest,incomingData,len);
    
    //re-init response object
    gwResponse = emptyResponse;
    gwResponse.opResponses_count = decodedRequest.operations_count;

    //call handlers
    int count;
    for (count = 0; count < decodedRequest.operations_count; count++)
    {
        auto &op = decodedRequest.operations[count].op;
        auto &which_op = decodedRequest.operations[count].which_op;
        response_OpResponse &rsp = gwResponse.opResponses[count];

        switch (which_op)
        {
        case request_Operation_send_tag:
            sendHandler(mac_addr, op.send, rsp);
            break;
        case request_Operation_qRequest_tag:
            subscribeHandler(mac_addr, op.qRequest, rsp);
            break;
        case request_Operation_ping_tag:
            pingHandler(mac_addr, op.ping, rsp);
            break;
        default:
            break;
        }
    }
    //send back response
    u8_t outputBuffer[EN2MC_BUFFERSIZE];
    int outputBufferLen = serializeResponse( outputBuffer, gwResponse );

    eNowUtil.send(mac_addr,outputBuffer, outputBufferLen);
}

void EspNow2MqttGateway::pingHandler(const uint8_t * mac_addr, request_Ping & ping, response_OpResponse & rsp)
{
    buildResponse(response_Result_OK, NULL, rsp);
}
void EspNow2MqttGateway::sendHandler(const uint8_t * mac_addr, request_Send & send, response_OpResponse & rsp)
{
    //TODO: implement
    buildResponse(response_Result_NOK, "sin implementar", rsp);
}
void EspNow2MqttGateway::subscribeHandler(const uint8_t * mac_addr, request_Subscribe & subs, response_OpResponse & rsp)
{
    //TODO: implement
    buildResponse(response_Result_NOK, "sin implementar", rsp);
}

inline void EspNow2MqttGateway::buildResponse(response_Result code, char * payload , response_OpResponse & rsp)
{
    rsp.result_code = code;
    if(payload){
        strlcpy(rsp.payload, payload, sizeof(rsp.payload));
    }
}

void EspNow2MqttGateway::deserializeRequest(request &rq, const uint8_t *incomingData, int len)
{
    //decrypt
    uint8_t decripted[len];
    crmsg.decrypt(decripted, incomingData, len);

    //deserialize
    pb_istream_t iStream = pb_istream_from_buffer(decripted, len);
    pb_decode(&iStream, request_fields, &rq);
}

inline int EspNow2MqttGateway::serializeResponse (u8_t * buffer, response &rsp)
{
    //serialize
    u8_t serializedBuffer[EN2MC_BUFFERSIZE];
    int bufferLen = 0;
    pb_ostream_t myStream = pb_ostream_from_buffer(buffer, EN2MC_BUFFERSIZE);
    pb_encode (&myStream, response_fields, &rsp);
    int messageLength = myStream.bytes_written;

    //encrypt
    crmsg.encrypt(buffer,serializedBuffer,bufferLen);

    return messageLength;
}


#endif // _ESPNOW2MQTTGATEWAY_HPP_
