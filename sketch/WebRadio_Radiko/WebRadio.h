/*
 * https://twitter.com/wakwak_koba/
 */

#ifndef _WAKWAK_KOBA_WEBRADIO_HPP_
#define _WAKWAK_KOBA_WEBRADIO_HPP_

#include <functional>
#include <vector>
#include <nvs.h>

static void voidDownloadTask(void *radio);
static void voidDecodeTask(void *radio);

class WebRadio {
  public:
    class Station {
      public:
        virtual const char * getName()  { return nullptr; }
        virtual bool play() { return radio->play(this); }
      protected:
        Station(WebRadio * _radio) : radio(_radio) {}
        WebRadio * radio = nullptr;
    };
  protected:
    WebRadio(AudioOutput * _out) : out(_out) {}
    
    WebRadio(AudioOutput * _out, byte cpuDecode, uint16_t stackDecode) : out(_out) {
      xTaskCreatePinnedToCore(&voidDecodeTask  , "decode"  , stackDecode  , this, 1, &decode_handle  , cpuDecode  ); 
    }
    
    WebRadio(AudioOutput * _out, byte cpuDecode, uint16_t stackDecode, byte cpuDownload, uint16_t stackDownload) : out(_out) {
      xTaskCreatePinnedToCore(&voidDecodeTask  , "decode"  , stackDecode  , this, 1, &decode_handle  , cpuDecode  ); 
      xTaskCreatePinnedToCore(&voidDownloadTask, "download", stackDownload, this, 1, &download_handle, cpuDownload);
    }
    
    ~WebRadio() {
      if(download_handle)
        vTaskDelete(download_handle);
      if(decode_handle)
        vTaskDelete(decode_handle);
      deInit();
    }

    int getIndex(Station * station) {
      for(int i = 0; i < stations.size(); i++)
        if(stations[i] == station)
          return i;
      return -1;
    }
   
    virtual void saveStation(WebRadio::Station * station) {
      uint32_t nvs_handle;
      if (!nvs_open("WebRadio", NVS_READWRITE, &nvs_handle)) {
        saveStationCore(nvs_handle, station);
        nvs_close(nvs_handle);
      }
    }
    
    virtual WebRadio::Station * restoreStation() {
      WebRadio::Station * result = nullptr;
      uint32_t nvs_handle;
      if (!nvs_open("WebRadio", NVS_READONLY, &nvs_handle)) {
        result = restoreStationCore(nvs_handle);
        nvs_close(nvs_handle);
      }
      return result;
    }
    
    virtual void deInit() {
      for (auto itr : stations)
        delete itr;
      stations.clear();
    }
    
    virtual void saveStationCore(uint32_t nvs_handle, WebRadio::Station * station) {}
    virtual WebRadio::Station * restoreStationCore(uint32_t nvs_handle) {return nullptr;}  

    virtual void downloadTask() {}
    virtual void decodeTask() {}

  public:
    virtual bool begin() { return false; }
    virtual bool play(Station * station = nullptr) { return false; }
    virtual bool play(bool next) { return false; }    // true:next false:previous
    virtual void stop() {}
    
    virtual size_t getNumOfStations() { return stations.size(); }
    virtual Station * getStation(size_t index) {
      if(index >= stations.size())
        return nullptr;

      return stations[index];
    }
    
    virtual bool RegisterMetadataCB(AudioStatus::metadataCBFn fn, void *data) {
      fnCbMetadata = fn;
      fnCbMetadata_data = data;
      return true;
    }

    virtual bool RegisterStatusCB(AudioStatus::statusCBFn fn, void *data) {
      fnCbStatus = fn;
      fnCbStatus_data = data;
      return true;
    }
    
    String getInfoStack() {
      if(download_handle && decode_handle)
        return "Stack: Decode:" + String(uxTaskGetStackHighWaterMark(decode_handle)) + " Download:" + String(uxTaskGetStackHighWaterMark(download_handle));
      else if(decode_handle)
        return "Stack: Decode:" + String(uxTaskGetStackHighWaterMark(decode_handle));
      else
        return "";
    }

    std::function<void(const char *station_name, const size_t station_index)> onPlay = nullptr;

  protected:
    AudioOutput * out = nullptr;
    std::vector<Station *> stations;
    
    TaskHandle_t download_handle;
    TaskHandle_t decode_handle;
    
    AudioStatus::metadataCBFn fnCbMetadata = nullptr;
    void *fnCbMetadata_data = nullptr;
    AudioStatus::statusCBFn fnCbStatus = nullptr;
    void *fnCbStatus_data = nullptr;
    
  private:
    static void voidDownloadTask(void *radio) {
      ((WebRadio*)radio)->downloadTask();
    }
    
    static void voidDecodeTask(void *radio) {
      ((WebRadio*)radio)->decodeTask();
    }
};

#endif
