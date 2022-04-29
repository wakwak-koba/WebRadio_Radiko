/*
 * https://twitter.com/wakwak_koba/
 */

#ifndef _WAKWAK_KOBA_WEBRADIO_HPP_
#define _WAKWAK_KOBA_WEBRADIO_HPP_

class WebRadio;

class WebRadio {
  public:
    class Station {
      public:
        Station(WebRadio * _radio) : radio(_radio) {}
        const char * getName()  { return name.c_str();}
        virtual bool play() { return radio->play(this); }
      public:
        String name;
      protected:
        WebRadio * radio = nullptr;
    };
  
  public:
    WebRadio(AudioOutput * _out) : out(_out) {}
    
    virtual bool begin() {
      return begin(APP_CPU_NUM);
    }
    virtual bool begin(int cpuDownload) {
      return begin(cpuDownload, 1 - cpuDownload);
    }
    virtual bool begin(int cpuDownload, int cpuDecode) { return false; }
    
    virtual size_t getNumOfStations() { return 0; }
    virtual Station * getStation(size_t index) { return nullptr; }
    virtual bool play(Station * station = nullptr) { return false; }
    String getInfoStack() {
      return "Stack: Download:" + String(uxTaskGetStackHighWaterMark(download_handle)) + " Decode:" + String(uxTaskGetStackHighWaterMark(decode_handle));
    }

    std::function<void(const char *station_name, const size_t station_index)> onPlay = nullptr;

  protected:
    AudioOutput * out = nullptr;
    TaskHandle_t download_handle;
    TaskHandle_t decode_handle;
};


#endif
