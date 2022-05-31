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
static const char * headerKeys[] = {"x-radiko-authtoken", "x-radiko-keyoffset", "x-radiko-keylength"} ;
static const size_t numberOfHeaders = sizeof(headerKeys) / sizeof(headerKeys[0]);
  
static void getInner(const String & source, const String & tagF, const String & tagT, std::function<void(const String &)> action, bool removeTagF = false) {
  int idxF = 0;
  for(;;) {
    idxF = source.indexOf(tagF, idxF);
    int idxT = source.indexOf(tagT, idxF);
    if(idxF < 0 || idxT < 0) break;
    action(source.substring(idxF + (removeTagF ? tagF.length() : 0), idxT));
    idxF = idxT + tagT.length();
  }
}

static void getInner(const String & source, const String & tag, std::function<void(const String &)> action) {
  getInner(source, "<" + tag + ">", "</" + tag + ">", action, true);
}

class Radiko : public WebRadio {
  public:
    Radiko(AudioOutput * _out, int cpuDecode, const uint16_t buffSize = 6 * 1024) : buffer(buffSize), WebRadio(_out, cpuDecode, 2048, 1 - cpuDecode, 2560) {}

    ~Radiko() {
      if(decoder)
        delete decoder;
    }
    
    class station_t : public WebRadio::Station {
      public:
        String id;
        String name;
        String logo;
//      String href;

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
              auto decoder = new AudioGeneratorAAC();
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
              
              clearChunks();

              if (http.begin(client, url)) {
                http.addHeader("X-Radiko-AuthToken", getRadiko()->token);
                auto httpCode = http.GET();
                if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
                  getInner(http.getString(), "http://", "\n", [this](const String & value) {
                    chunks.push_back(new chunk_t(this, value));
                  });
                http.end();
              }
              return &chunks;
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
          
          clearPlaylists();

          char url[100];
          sprintf(url, "http://f-radiko.smartstream.ne.jp/%s/_definst_/simul-stream.stream/playlist.m3u8", id);
          if (http.begin(client, url)) {
            http.addHeader("X-Radiko-AuthToken", getRadiko()->token);
            auto httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
              getInner(http.getString(), "http://", "\n", [this](const String & value) {
                playlists.push_back(new playlist_t(this, value));
              });
            http.end();
          }
          return &playlists;
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

    virtual bool begin() override {
      WiFiClientSecure clients;
      HTTPClient http;
      String partialkey;
      String token;
      String area;
      
      deInit();

      clients.setInsecure();
      if (http.begin(clients, "https://radiko.jp/v2/api/auth1")) {
        http.addHeader("User-Agent","arduino");
        http.addHeader("Accept","*/*");       
        http.addHeader("X-Radiko-App","pc_html5");
        http.addHeader("X-Radiko-App-Version","0.0.1");
        http.addHeader("X-Radiko-User","dummy_user");
        http.addHeader("X-Radiko-Device","pc");
        http.collectHeaders(headerKeys, numberOfHeaders);
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          auto offset = http.header(headerKeys[1]).toInt();
          auto length = http.header(headerKeys[2]).toInt();
    
          char key[length + 1];
          strncpy(key, &secret_key[offset], length);
          key[length] = 0;
    
          partialkey = base64::encode(key);
          token = http.header(headerKeys[0]);
        }   
        http.end();
      }

      if (http.begin(clients, "https://radiko.jp/v2/api/auth2")) {
        http.addHeader("X-Radiko-AuthToken", token);
        http.addHeader("X-Radiko-Partialkey", partialkey);
        http.addHeader("X-Radiko-User","dummy_user");
        http.addHeader("X-Radiko-Device","pc");
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          auto payload = http.getString();
          area = payload.substring(0, payload.indexOf(","));
        }   
        http.end();
      }

      if(!area.length() || !token.length())
        return false;
        
      char url[40 + area.length()];
      sprintf(url, "https://radiko.jp/v2/station/list/%s.xml", area.c_str());
      if (http.begin(clients, url)) {
        auto httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          getInner(http.getString(), "station", [&](const String & value) {
            auto station = new station_t(this);
            getInner(value, "id"         , [station](const String & value) { station->id   = value; });
            getInner(value, "name"       , [station](const String & value) { station->name = value; });
            getInner(value, "logo_xsmall", [station](const String & value) { station->logo = value; });
            stations.push_back(station);
          } );
        }
        http.end();
      }

      if(!stations.size())
        return false;

      this->token = new char[token.length() + 1];
      strcpy(this->token, token.c_str());

      this->area = new char[area.length() + 1];
      strcpy(this->area, area.c_str());

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
          chunk_index = 0;
        }

        if(chunks && !stream) {
          if(chunk_index >= chunks->size())
            chunks = nullptr;
          else {
            auto chunk = (*chunks)[chunk_index];
            chunk_index++;
            
            if(onChunk) {
              auto bufs = chunk->toString().c_str();
              char text[7 + strlen(bufs)];
              sprintf(text, "%2d/%2d %s", chunk_index, chunks->size(), chunk->toString().c_str());
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
      for(;;) {
        delay(1);
        
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
        } else if (!decoder->isRunning() && buffer.isFilled()) {
          if(!decoder->begin(&buffer, out))
            delay(1000);
        } else if (decoder->isRunning() && buffer.getSize() >= 2*1024 && !decoder->loop()) {
          decoder->stop();
          nextChunk = true;
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
        if(playlists->size() > 0)
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

    char *token = nullptr;
    char *area = nullptr;

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

      if(area) {
        delete []area;
        area = nullptr;
      }
      if(token) {
        delete []token;
        token = nullptr;
      }
    }

    virtual void saveStationCore(uint32_t nvs_handle, WebRadio::Station * station) override {
      char key[8 + strlen(area)];
      sprintf(key, "radiko_%s", area);
      nvs_set_str(nvs_handle, "radiko", ((station_t *)station)->id.c_str());
      nvs_set_str(nvs_handle, key     , ((station_t *)station)->id.c_str());
    }

    virtual WebRadio::Station * restoreStationCore(uint32_t nvs_handle) override {
      WebRadio::Station * result = nullptr;
      size_t length = 0;
      char *value;

      // 同一エリアの前回局
      char key[8 + strlen(area)];   
      sprintf(key, "radiko_%s", area);
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
