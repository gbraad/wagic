#include <pspkernel.h>
#include <pspdebug.h>
#include <stdio.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <psputility.h>

#include "../include/JAudio.h"
#include "../include/JFileSystem.h"
#include "../include/JMP3.h"


JMP3* JMP3::mInstance = NULL;



void JMP3::init() {
   loadModules();
}

JMP3::JMP3(const std::string& filename, int inBufferSize, int outBufferSize) :
  m_volume(PSP_AUDIO_VOLUME_MAX), m_samplesPlayed(0), m_paused(true) {
   load(filename, inBufferSize,outBufferSize);
}

JMP3::~JMP3() {
   unload();
}

bool JMP3::loadModules() {
   int loadAvCodec = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
   if (loadAvCodec < 0) {
      return false;
   }

   int loadMp3 = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
   if (loadMp3 < 0) {
      return false;
   }

   return true;
}

bool JMP3::fillBuffers() {
   SceUChar8* dest;
   SceInt32 length;
   SceInt32 pos;

   int ret = sceMp3GetInfoToAddStreamData(m_mp3Handle, &dest, &length, &pos);
   if (ret < 0)
      return false;

   if (sceIoLseek32(m_fileHandle, pos, SEEK_SET) < 0) {
      return false;
   }

   int readLength = sceIoRead(m_fileHandle, dest, length);
   if (readLength < 0)
      return false;

   ret = sceMp3NotifyAddStreamData(m_mp3Handle, readLength);
   if (ret < 0)
      return false;

   return true;
}

bool JMP3::load(const std::string& filename, int inBufferSize, int outBufferSize) {
   printf("load\n");
      m_inBufferSize = inBufferSize;
      printf("1\n");
      //m_inBuffer = new char[m_inBufferSize];
      //if (!m_inBuffer)
      //   return false;

      m_outBufferSize = outBufferSize;
      printf("2\n");
      //m_outBuffer = new short[outBufferSize];
      //if (!m_outBuffer)
      //   return false;

      printf("3:%s\n",filename.c_str());
      m_fileHandle = sceIoOpen(filename.c_str(), PSP_O_RDONLY, 0777);
      if (m_fileHandle < 0)
         return false;

      printf("4\n");
      int ret = sceMp3InitResource();
      if (ret < 0)
         return false;

      SceMp3InitArg initArgs;

      int fileSize = sceIoLseek32(m_fileHandle, 0, SEEK_END);
      sceIoLseek32(m_fileHandle, 0, SEEK_SET);

      unsigned char* testbuffer = new unsigned char[7456];
      sceIoRead(m_fileHandle, testbuffer, 7456);

      delete testbuffer;

      initArgs.unk1 = 0;
      initArgs.unk2 = 0;
      initArgs.mp3StreamStart = 0;
      initArgs.mp3StreamEnd = fileSize;
      initArgs.mp3BufSize = m_inBufferSize;
      initArgs.mp3Buf = (SceVoid*) m_inBuffer;
      initArgs.pcmBufSize = m_outBufferSize;
      initArgs.pcmBuf = (SceVoid*) m_outBuffer;

      printf("5\n");
      m_mp3Handle = sceMp3ReserveMp3Handle(&initArgs);
      if (m_mp3Handle < 0)
         return false;

      // Alright we are all set up, let's fill the first buffer.
      printf("5.5\n");
      bool _filled= fillBuffers();
      if (! _filled) return false;

      printf("end = %i, bufsize = %i, outSize = %i\n", fileSize, m_inBufferSize, m_outBufferSize);

      // Start this bitch up!
      printf("6\n");
      int start = sceMp3Init(m_mp3Handle);
      if (start < 0)
         return false;

      printf("7\n");
      m_numChannels = sceMp3GetMp3ChannelNum(m_mp3Handle);
      printf("8\n");
      m_samplingRate = sceMp3GetSamplingRate(m_mp3Handle);

   return true;
}

bool JMP3::unload() {
  printf("unload 1\n");
   if (m_channel >= 0)
      sceAudioSRCChRelease();

   printf("unload 2\n");
   sceMp3ReleaseMp3Handle(m_mp3Handle);

   printf("unload 3\n");
   sceMp3TermResource();

   printf("unload 4\n");
   sceIoClose(m_fileHandle);

   printf("unload 5\n");
   //delete[] m_inBuffer;

    printf("unload 6\n");
   //delete[] m_outBuffer;

   printf("unload 7\n");
   return true;
}

bool JMP3::update() {
   if (!m_paused) {
      if (sceMp3CheckStreamDataNeeded(m_mp3Handle) > 0) {
         fillBuffers();
      }

      short* tempBuffer;
      int numDecoded = 0;

      while (true) {
         numDecoded = sceMp3Decode(m_mp3Handle, &tempBuffer);
         if (numDecoded > 0)
            break;

         int ret = sceMp3CheckStreamDataNeeded(m_mp3Handle);
         if (ret <= 0)
            break;

         fillBuffers();
      }

      // Okay, let's see if we can't get something outputted :/
      if (numDecoded == 0 || ((unsigned)numDecoded == 0x80671402)) {
         sceMp3ResetPlayPosition(m_mp3Handle);
         if (!m_loop)
            m_paused = true;

         m_samplesPlayed = 0;
      } else {
         if (m_channel < 0 || m_lastDecoded != numDecoded) {
            if (m_channel >= 0)
               sceAudioSRCChRelease();

            m_channel = sceAudioSRCChReserve(numDecoded / (2 * m_numChannels), m_samplingRate, m_numChannels);
         }

         // Output
         m_samplesPlayed += sceAudioSRCOutputBlocking(m_volume, tempBuffer);
         m_playTime = (m_samplingRate > 0) ? (m_samplesPlayed / m_samplingRate) : 0;
      }
   }

   return true;
}

bool JMP3::play() {
   return (m_paused = false);
}

bool JMP3::pause() {
   return (m_paused = true);
}

bool JMP3::setLoop(bool loop) {
   sceMp3SetLoopNum(m_mp3Handle, (loop == true) ? -1 : 0);
   return (m_loop = loop);
}

int JMP3::setVolume(int volume) {
   return (m_volume = volume);
}

int JMP3::playTime() const {
   return m_playTime;
}

int JMP3::playTimeMinutes() {
   return m_playTime / 60;
}

int JMP3::playTimeSeconds() {
   return m_playTime % 60;
}
