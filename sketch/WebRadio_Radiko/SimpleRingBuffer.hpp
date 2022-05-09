/*
 * https://twitter.com/wakwak_koba/
 */

#ifndef _WAKWAK_KOBA_SIMPLE_RINGBUFFER_HPP_
#define _WAKWAK_KOBA_SIMPLE_RINGBUFFER_HPP_

template <typename T>
class SimpleRingBuffer {
  public:
    SimpleRingBuffer(uint16_t _buffSize = 6000) : buffSize(_buffSize), writeMode(0), deallocateBuffer(true) {
      buffer = new T[buffSize];
      if(!buffer)
        Serial.printf("failed buffer = new T[%d]\n", buffSize);
      init();
    }

    SimpleRingBuffer(T *_buffer, uint16_t _buffSize) : buffSize(_buffSize), buffer(_buffer), writeMode(0), deallocateBuffer(false) {
      init();
    }

    ~SimpleRingBuffer() {
      if(deallocateBuffer)
        delete []buffer;
    }

    void init() {
      readPtr = 0;
      writePtr = 0;
      readSum = 0;
      writeSum = 0;
    }
    
    uint16_t available() {
      if (readPtr > writePtr)
        return buffSize + writePtr - readPtr;
      else
        return            writePtr - readPtr;
    }

    uint16_t free() {
      return buffSize - available() - 1;
    }
    
    uint16_t write(T *payload, uint16_t length) {
      if (!payload || !length || length > buffSize) return 0;

      auto f = free();
      if (length > f) {
        if (writeMode == 2)
          readPtr = (readPtr + length - f) % buffSize;
        else {
          if (writeMode == 1)
            length = f;
          if (!length || !writeMode)
            return 0;
        }
      }

      auto bytesAvail = std::min(length, (uint16_t)(buffSize - writePtr));
      memcpy(&buffer[writePtr], &payload[0         ], sizeof(T) * bytesAvail);
      memcpy(&buffer[0       ], &payload[bytesAvail], sizeof(T) * (length - bytesAvail));
      writePtr = (writePtr + length) % buffSize;
      writeSum += length;

      return length;
    }

    uint16_t write(std::function<uint16_t(T * payload, uint16_t length)> func, uint16_t length = UINT16_MAX) {     
/*    if(!length)
        length = f;
      else if (length > f) {
        if (writeMode == 2)
          readPtr = (readPtr + length - f) % buffSize;
        else {
          if (writeMode == 1)
            length = f;
          if (!length || !writeMode)
            return 0;
        }
      }
      if(!length)
        return 0;*/

      auto f = free();
      length = std::min(length, f);
      if(!length) return 0;
      
      uint16_t wlen = 0;
      auto bytesAvail = std::min(length, (uint16_t)(buffSize - writePtr));
      if(bytesAvail > 0)
        wlen = func(&buffer[writePtr], bytesAvail);

      if(wlen >= bytesAvail && length - wlen > 0)
        wlen += func(&buffer[0], length - wlen );
      
      writePtr = (writePtr + wlen) % buffSize;
      writeSum += wlen;

//   Serial.printf(">> write: length:%4u free:%4u writePtr:%4u readPtr:%4u writeSum:%8u readSum:%8u margin:%8u\n", wlen, f, writePtr, readPtr, writeSum, readSum, writeSum - readSum);
      
      return wlen;
    }

    uint16_t read(T *data, uint16_t length) {
      length = std::min(length, available());
      if (!data || !length) return 0;

//    Serial.printf("<< read : length:%4u free:%4u writePtr:%4u readPtr:%4u writeSum:%8u readSum:%8u margin:%8u\n", length, free(), writePtr, readPtr, writeSum, readSum, writeSum - readSum);

      auto bytesAvail = std::min(length, (uint16_t)(buffSize - readPtr));
      memcpy(&data[0         ], &buffer[readPtr], sizeof(T) * bytesAvail);
      memcpy(&data[bytesAvail], &buffer[0      ], sizeof(T) * (length - bytesAvail));
      readPtr = (readPtr + length) % buffSize;
      readSum += length;

//    Serial.printf(">> read : length:%4u free:%4u writePtr:%4u readPtr:%4u writeSum:%8u readSum:%8u margin:%8u\n", length, free(), writePtr, readPtr, writeSum, readSum, writeSum - readSum);

      return length;
    }

    uint16_t seek(uint16_t length) {
      uint16_t result = std::min( available(), length );
      readPtr = (readPtr + length) % buffSize;
      return result;
    }

    String getInfo() {
      return "Available:" + String(available()) + " Free:" + String(free()) + " WritePtr:" + String(writePtr) + " ReadPtr:" + String(readPtr) + " WriteSum:" + String(writeSum) + " readSum:" + String(readSum);
    }

  private:
    T *buffer;
    uint16_t buffSize;
    uint16_t readPtr;
    uint16_t writePtr;
    uint8_t writeMode;     // 空きが足りないときの挙動 0:writeしない 1:空きの分だけ 2:先頭を削除して強制挿入
    bool deallocateBuffer;
    uint32_t writeSum;
    uint32_t readSum;
};

#endif
