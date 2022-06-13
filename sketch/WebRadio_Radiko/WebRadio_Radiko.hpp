/*
 * https://twitter.com/wakwak_koba/
 */

#ifndef _WAKWAK_KOBA_WEBRADIO_RADIKO_HPP_
#define _WAKWAK_KOBA_WEBRADIO_RADIKO_HPP_

#include <AudioGeneratorAAC.h>
#include <AudioFileSourceHTTPStream.h>
#include "AudioFileSourceHLS.hpp"
#include <base64.h>

#ifdef AAC_ENABLE_SBR
#error "Please switch AAC_ENABLE_SBR from enabled to disabled"
#endif

#include "WebRadio.h"

static const char * secret_key = "bcd151073c03b352e1ef2fd66c32209da9ca0afa";
// static const char * secret_key = "7c0bbfb35c926d0040c9fba03c23a78864eb31c3......";   // Find out the key yourself (16000Bytes / 32000Chars)

static const char * headerKeys[] = {"x-radiko-authtoken", "x-radiko-keyoffset", "x-radiko-keylength", "Set-Cookie"};

static const char * X_Radiko_App[]          = {"pc_html5", "aSmartPhone7a"};
static const char * X_Radiko_App_Version[]  = {"0.0.1", "7.4.6"};
static const char * X_Radiko_User           = "dummy_user";
static const char * X_Radiko_Device         = "pc";

static void getInner(const String & source, const String & tagF, const String & tagT, std::function<void(const String &)> action, bool removeTagF = false) {
  int idxF = 0;
  for(;;) {
    idxF = source.indexOf(tagF, idxF);
    int idxT = source.indexOf(tagT, idxF + tagF.length());
    if(idxF < 0 || idxT < 0) break;
    action(source.substring(idxF + (removeTagF ? tagF.length() : 0), idxT));
    idxF = idxT + tagT.length();
  }
}

static void getInner(Stream * source, const String & tagF, const String & tagT, std::function<void(const String &)> action, bool removeTagF = false) {
  for(;;) {
    if(!source->find(tagF.c_str()))
      break;

    String value;
    if(!removeTagF)
      value = tagF;
      
    for(;;) {
      auto c = source->read();
      if(c < 0) {
        action(value);
        return;
      }
      value.concat((char)c);
      if(value.endsWith(tagT)) {
        action(value.substring(0, value.length() - tagT.length()));
        break;
      }
    }
  }
}

static void getInner(const String & source, const String & tag, std::function<void(const String &)> action) {
  getInner(source, "<" + tag + ">", "</" + tag + ">", action, true);
}

static void getInner(Stream * source, const String & tag, std::function<void(const String &)> action) {
  getInner(source, "<" + tag + ">", "</" + tag + ">", action, true);
}

static uint8_t asc2byte(const char chr) {
  uint8_t rVal = 0;
  if (isdigit(chr))
    rVal = chr - '0';
  else if (chr >= 'A' && chr <= 'F')
    rVal = chr + 10 - 'A';
  else if (chr >= 'a' && chr <= 'f')
    rVal = chr + 10 - 'a';
  return rVal;
}

static void unHex(const char * inP, uint8_t * outP, size_t len) {
  for (; len > 1; len -= 2) {
    uint8_t val = asc2byte(*inP++) << 4;
    *outP++ = val | asc2byte(*inP++);
  }
}

static String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
}

class Radiko : public WebRadio {
  public:
    Radiko(AudioOutput * _out, int cpuDecode, const uint16_t buffSize = 6 * 1024) : buffer(buffSize), WebRadio(_out, cpuDecode, 2048, 1 - cpuDecode, 2560) {
      decode_buffer = malloc(decode_buffer_size);
    }

    ~Radiko() {
      setAuthorization();
      if(decoder)
        delete decoder;
      free(decode_buffer);
    }
    
    class station_t : public WebRadio::Station {
      public:
        String id;
        String name;
        String area;

        station_t(Radiko* _radiko) : Station(_radiko) {;}
        ~station_t() {
          clearPlaylists();
        }

        virtual const char * getName() override { return name.c_str(); }
        virtual bool play() override {
          getRadiko()->play(this);
          return true;
        }

        Radiko * getRadiko() {
          return (Radiko *)radio;
        }

        struct playlist_t {
          private:
            station_t* station;
            String url;
            AudioGeneratorAAC * decoder = nullptr;
          public:
            playlist_t(station_t* _station, const String & _url) : station(_station), url(_url) {;}
            ~playlist_t() {
              clearChunks();
            }

            bool play() {
              getRadiko()->select_playlist = this;
              return true;
            }

            station_t * getStation() {
              return station;
            }

            Radiko * getRadiko() {
              return getStation()->getRadiko();
            }
            
            AudioGeneratorAAC * getDecoder() {
              auto radiko = getRadiko();
              AudioGeneratorAAC * decoder;
              if(radiko->decode_buffer != nullptr)
                decoder = new AudioGeneratorAAC(radiko->decode_buffer, radiko->decode_buffer_size);
              else
                decoder = new AudioGeneratorAAC();
              
              decoder->RegisterMetadataCB(radiko->fnCbMetadata, radiko->fnCbMetadata_data);
              decoder->RegisterStatusCB  (radiko->fnCbStatus  , radiko->fnCbStatus_data  );
              return decoder;
            }
            
            struct chunk_t {
              private:
                playlist_t* playlist;
                String url;
              public:
                chunk_t(playlist_t* _playlist, const String & _url) : playlist(_playlist), url(_url) {;}

                AudioFileSourceHTTPStream * getStream() {
                  auto radiko = playlist->getRadiko();
                  auto stream = new AudioFileSourceHTTPStream();
                  stream->RegisterMetadataCB(radiko->fnCbMetadata, radiko->fnCbMetadata_data);
                  stream->RegisterStatusCB  (radiko->fnCbStatus  , radiko->fnCbStatus_data  );
                  if(!stream->open(url.c_str())) {
                    delete stream;
                    stream = nullptr;
                  }
                  return stream;
                }

                String toString() {
                  return url;
                }
              private:
            };

            std::vector<chunk_t *> * getChunks() {
              WiFiClient client;
              HTTPClient http;
              bool success = false;
              
              clearChunks();

              if (http.begin(client, url)) {
                http.addHeader("X-Radiko-AuthToken", getRadiko()->token);
                auto httpCode = http.GET();
                if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                  getInner(http.getString(), "http://", "\n", [this](const String & value) {
                    chunks.push_back(new chunk_t(this, value));
                  });
                  success = true;
                }
                http.end();
              }
              if(success)
                return &chunks;
              else
                return nullptr;
            }

          private:
            void clearChunks() {
              for (auto itr : chunks)
                delete itr;
              chunks.clear();
            }
          private:
            std::vector<chunk_t *> chunks;
            
        };
          
        std::vector<playlist_t *> * getPlaylists() {
          WiFiClient client;
          HTTPClient http;
          bool success = false;
          
          clearPlaylists();

          char url[81 + id.length()];
          sprintf(url, "http://f-radiko.smartstream.ne.jp/%s/_definst_/simul-stream.stream/playlist.m3u8", id.c_str());
          if (http.begin(client, url)) {
            http.addHeader("X-Radiko-AuthToken", getRadiko()->token);
            auto httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
              getInner(http.getString(), "http://", "\n", [this](const String & value) {
                playlists.push_back(new playlist_t(this, value));
              });
              success = true;
            }
            http.end();
          }
          if(success)
            return &playlists;
          else
            return nullptr;
        }
        
        String getProgram() {
          String title;
          WiFiClient client;
          HTTPClient http;
          
          char url[40 + strlen(area.c_str())];
          sprintf(url, "http://radiko.jp/v3/program/now/%s.xml", area.c_str());
          if (http.begin(client, url)) {
            auto httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
              auto stream = http.getStreamPtr();
              stream->setTimeout(5000);
              String tagF = String("<station id=\"") + (String)id + String("\">");
              if(stream->find(tagF.c_str()) && stream->find("<title>"))
                title = stream->readStringUntil('<');
            }
            http.end();
          }
          return title;
        }

        String toString() {
          return area + "/" + id + "/" + name;
        }
      private:
          void clearPlaylists() {
            for (auto itr : playlists)
              delete itr;
            playlists.clear();
          }
      
      private:
        std::vector<playlist_t *> playlists;
    };

    void setAuthorization() {
      setAuthorization(nullptr, nullptr);
    }
    
    void setAuthorization(const char * user, const char *pass) {
      if(this->user) {
        delete this->user;
        this->user = nullptr;
      }
      if(this->pass) {
        delete this->pass;
        this->pass = nullptr;
      }

      if(strlen(user)) {
        this->user = new char[strlen(user) + 1];
        strcpy(this->user, user);

      }
      if(strlen(pass)) {
        this->pass = new char[strlen(pass) + 1];
        strcpy(this->pass, pass);
      }
    }

    void setLocation() {
      setLocation(0.0F, 0.0F);
    }
    
    void setLocation(uint8_t pref) {
      auto area = getLocation(pref);
      if(area != nullptr) {
        setLocation(area->lat, area->lon);
        select_pref = pref;
      }
    }
    
    void setLocation(float lat, float lon) {
      this->lat = lat;
      this->lon = lon;
    }

    virtual bool begin() override {
      if(strlen(secret_key) != 40 && strlen(secret_key) != 32000)
        return false;     
        
      uint8_t keyType = strlen(secret_key) == 40 ? 0 : 1;
      WiFiClientSecure clients;
      HTTPClient http;
      String partialkey;
      String cookie;
      
      deInit();
      areaFree = false;

      clients.setInsecure();
      if (user && pass && http.begin(clients, "https://radiko.jp/ap/member/login/login")) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
        
        String payload = "mail=" + urlencode(String(user)) + "&pass=" + urlencode(String(pass));
        auto httpCode = http.POST(payload);
        if (httpCode == HTTP_CODE_FOUND) {
          cookie = http.header(headerKeys[3]);
          auto semi = cookie.indexOf(";");
          if(semi >= 0)
            cookie = cookie.substring(0, semi + 1);
        }
        http.end();
      }

      if (cookie.length() && http.begin(clients, "https://radiko.jp/ap/member/webapi/member/login/check")) {
        http.addHeader("Cookie", cookie);
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          getInner(http.getString(), "\"areafree\":\"", "\"", [this](const String & value) {
            if(value.equals("1"))
              areaFree = true;
          }, true);
        }
        http.end();
      }
      
      if (http.begin(clients, "https://radiko.jp/v2/api/auth1")) {
        http.addHeader("X-Radiko-App", X_Radiko_App[keyType]);
        http.addHeader("X-Radiko-App-Version", X_Radiko_App_Version[keyType]);
        http.addHeader("X-Radiko-User", X_Radiko_User);
        http.addHeader("X-Radiko-Device", X_Radiko_Device);
        if(cookie.length())
          http.addHeader("Cookie", cookie);
        http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          auto offset = http.header(headerKeys[1]).toInt();
          auto length = http.header(headerKeys[2]).toInt();

          if(keyType == 0) {
            char key[length + 1];
            strncpy(key, &secret_key[offset], length);
            key[length] = 0;
            partialkey = base64::encode(key);
          } else if(keyType == 1) {
            uint8_t key[length];
            unHex(&secret_key[offset * 2], key, length * 2);
            partialkey = base64::encode(key, length);           
//          partialkey = base64::encode(&secret_key[offset], length);  // when uint8_t[]
          }          

          token = http.header(headerKeys[0]);            
        }   
        http.end();
      }

      if (partialkey.length() && http.begin(clients, "https://radiko.jp/v2/api/auth2")) {
        http.addHeader("X-Radiko-App", X_Radiko_App[keyType]);
        http.addHeader("X-Radiko-App-Version", X_Radiko_App_Version[keyType]);
        http.addHeader("X-Radiko-User", X_Radiko_User);
        http.addHeader("X-Radiko-Device", X_Radiko_Device);
        http.addHeader("X-Radiko-AuthToken", token);
        http.addHeader("X-Radiko-Partialkey", partialkey);
        http.addHeader("X-Radiko-Connection", "wifi");
        if(cookie.length())
          http.addHeader("Cookie", cookie);

        if(keyType == 1 && lat && lon) {
          char loc[30];
          sprintf(loc, "%.6f,%.6f,gps", lat, lon);
          http.addHeader("X-Radiko-Location", loc);
        }
        
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          auto payload = http.getString();
          area = payload.substring(0, payload.indexOf(","));
        }   
        http.end();
      }

      if(!area.length() || !token.length())
        return false;

      if(areaFree && !select_pref && http.begin(clients, "https://radiko.jp/v3/station/region/full.xml")) {
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          auto stream = http.getStreamPtr();
          stream->setTimeout(5000);
          while(stream->find("<station>")) {
            auto station = new station_t(this);
            if(stream->find("<id>"))
              station->id = stream->readStringUntil('<');
            if(stream->find("<name>"))
              station->name = stream->readStringUntil('<');
            if(stream->find("<area_id>"))
              station->area = stream->readStringUntil('<');
            stations.push_back(station);
          }
        }
        http.end();
      } else {
        if(cookie.length() && select_pref)
          area = "JP" + String(select_pref);
        char url[40 + area.length()];
        sprintf(url, "https://radiko.jp/v2/station/list/%s.xml", area.c_str());
        if (http.begin(clients, url)) {
          auto httpCode = http.GET();
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
              getInner(http.getStreamPtr(), "station", [&](const String & value) {
                auto station = new station_t(this);
                getInner(value, "id"         , [station](const String & value) { station->id   = value; });
                getInner(value, "name"       , [station](const String & value) { station->name = value; });
                station->area = area;
                stations.push_back(station);
              } );
          }
          http.end();
        }
      }

      if(!stations.size())
        return false;

      return true;
    }

    virtual void downloadTask() override {
      unsigned long long saveSettings = 0;
      int chunk_index;
      std::vector<station_t::playlist_t::chunk_t *> * chunks = nullptr;
      
      for(;;) {
        delay(10);

        if(select_station || select_playlist) {
          stop();
          if(current_station)
            delete current_station;
            
          chunks = nullptr;
          current_station = select_station;
          current_playlist = select_playlist;
          if(!current_station && current_playlist)
            current_station = current_playlist->getStation();
          if(onPlay)
            onPlay(current_station->getName(), getIndex(current_station));
          saveSettings = millis() + 10000;
          select_station  = nullptr;
          select_playlist = nullptr;
        }

        if(current_playlist && !decoder)
          decoder = current_playlist->getDecoder();

        if(current_playlist && !chunks) {
          chunks = current_playlist->getChunks();
          if(chunks == nullptr)
            select_playlist = current_playlist;
          else if(onProgram)
            onProgram(current_station->getProgram().c_str());
          chunk_index = 0;
        }

        if(chunks && !stream) {
          if(chunk_index >= chunks->size())
            chunks = nullptr;
          else {
            auto chunk = (*chunks)[chunk_index];
            chunk_index++;
            
            if(onChunk) {
              auto chunkText = chunk->toString();
              char text[7 + chunkText.length()];
              sprintf(text, "%2d/%2d %s", chunk_index, chunks->size(), chunkText.c_str());
              onChunk(text);
            }
            stream = chunk->getStream();
            if(stream)
              buffer.setSource(stream);
          }
        }
    
        if(stream && stream->getPos() >= stream->getSize())
          nextChunk = true;

        if(stream)
          if(!buffer.fill())
            nextChunk = true;

        if(nextChunk) {
          buffer.setSource(nullptr);
          if(stream) {
            delete stream;
            stream = nullptr;
          }
          nextChunk = false;
        }           
        
        if (saveSettings > 0 && millis() > saveSettings) {
          saveStation(current_station);
          saveSettings = 0;
        }
      }  
    }

    virtual void decodeTask() override {
      auto last_loop = millis();
      
      for(;;) {
        delay(1);
        auto now_millis = millis();
        
        if(stopDecode > 0) {
          if(decoder) {
            if(decoder->isRunning())
              decoder->stop();
            delete decoder;
            decoder = nullptr;
          }
          stopDecode--;
          while(stopDecode) {delay(100);}
        }
        else if(!decoder) {
          ;
        } else if (!decoder->isRunning()) {
          if(buffer.isFilled() && !decoder->begin(&buffer, out)) {
            delay(1000);
            last_loop = now_millis;
          } else if (now_millis - last_loop > 10000) {
            if(onError)
              onError("Streaming can't begin");
//          decoder->stop();
            last_loop = now_millis;
            nextChunk = true;
          }
        } else if (decoder->isRunning()) {
          if(buffer.getSize() >= 2*1024) {
            if(decoder->loop())
              last_loop = now_millis;
            else {
              decoder->stop();
              nextChunk = true;
            }
          } else if (now_millis - last_loop > 5000) {
            if(onError)
              onError("Streaming reception time out");
            decoder->stop();
            nextChunk = true;
          }
        }
      }
    }

    virtual void stop() override {
      if(decoder) {
        stopDecode = 2;
        while(stopDecode == 2) {delay(100);}
        buffer.init();      
      }
      
      if (stream) {
        buffer.setSource(nullptr);
        delete stream;
        stream = nullptr;
      }
      stopDecode = 0;
    }
    
    virtual bool RegisterMetadataCB(AudioStatus::metadataCBFn fn, void *data) override {
      return WebRadio::RegisterMetadataCB(fn, data) && buffer.RegisterMetadataCB(fn, data);
    }
    virtual bool RegisterStatusCB(AudioStatus::statusCBFn fn, void *data) override {
      return WebRadio::RegisterStatusCB(fn, data) && buffer.RegisterStatusCB(fn, data);
    }

    virtual bool play(WebRadio::Station * station = nullptr) override {
      if(!station)
        station = restoreStation();

      if(!station && stations.size() > 0)
        station = stations[0];

      if(station) {
        auto playlists = ((station_t *)station)->getPlaylists();
        if(playlists && playlists->size() > 0)
          select_playlist = (*playlists)[0];
        else
          select_station = (station_t *)station;
        return true;
      }
      return false;        
    }

    virtual bool play(bool next) override {
      auto sn = getNumOfStations();
      return play(getStation((getIndex(current_station) + sn + (next ? 1 : -1)) % sn));
    }

    String getInfoBuffer() {
      return buffer.getInfoBuffer();
    }
    
  public:
    std::function<void(const char * text)> onChunk = nullptr;
    
  private:
    AudioFileSourceHLS buffer;
    AudioGeneratorAAC * decoder = nullptr;
    AudioFileSource * stream = nullptr;
    void * decode_buffer = nullptr;
    size_t decode_buffer_size = 26352;
    
    char * user = nullptr;;
    char * pass = nullptr;;
    float lat = 0.0F;
    float lon = 0.0F;
    bool areaFree = false;

    String token;
    String area;

    uint8_t                 select_pref = 0;
    station_t             * select_station  = nullptr;
    station_t::playlist_t * select_playlist = nullptr;
    station_t             * current_station  = nullptr;
    station_t::playlist_t * current_playlist = nullptr;
    
    bool nextChunk = false;
    byte stopDecode = 0;    // 2:request stop 1:accept request

    AudioStatus::metadataCBFn fnCbMetadata = nullptr;
    void *fnCbMetadata_data = nullptr;
    AudioStatus::statusCBFn fnCbStatus = nullptr;
    void *fnCbStatus_data = nullptr;
    
  private:
    void deInit() {
      WebRadio::deInit();
    }
    
    virtual void saveStationCore(uint32_t nvs_handle, WebRadio::Station * station) override {
      char key[8 + area.length()];
      sprintf(key, "radiko_%s", area.c_str());
      nvs_set_str(nvs_handle, "radiko", ((station_t *)station)->id.c_str());
      nvs_set_str(nvs_handle, key     , ((station_t *)station)->id.c_str());
    }

    virtual WebRadio::Station * restoreStationCore(uint32_t nvs_handle) override {
      WebRadio::Station * result = nullptr;
      size_t length = 0;
      char *value;

      // 同一エリアの前回局
      char key[8 + area.length()];   
      sprintf(key, "radiko_%s", area.c_str());
      nvs_get_str(nvs_handle, key, nullptr, &length);
      if(length) {
        value = new char[length];
        nvs_get_str(nvs_handle, key, value  , &length);
        for(auto itr : stations) {
          if(((station_t *)itr)->id.equals(value)) {
            result = itr;
            break;
          }
        }
        delete []value;
      }
      
      // 前回局が存在するか
      length = 0;
      nvs_get_str(nvs_handle, "radiko", nullptr, &length);
      if(length) {
        value = new char[length];
        nvs_get_str(nvs_handle, "radiko", value  , &length);
        for(auto itr : stations) {
          if(((station_t *)itr)->id.equals(value))
          {
            result = itr;
            break;
          }
        }
        delete []value;
      }  
      return result;  
    }
};

#endif
