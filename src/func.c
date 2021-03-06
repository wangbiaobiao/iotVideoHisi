/*
 * func.c
 *
 *  Created on: Dec 4, 2017
 *      Author: lichen
 */

#include "queue.h"
#include "cJSON.h"
#include "iotRtmp.h"
#include "iotMqtt.h"
#include "sample_comm.h"


int func_MqttGetCmd(uint8_t cmdbuf[])
{
  int cmdid = 0;
  MQTTMessage *message;

  message = iotMqtt_GetMessage();

  if (message) {

    cJSON *json = cJSON_Parse((char*)message->payload);

    if (json) {

      cJSON *type = cJSON_GetObjectItem(json, "type");
      if (type && !strcmp(type->valuestring, "video")) {
        cJSON *jcmdid = cJSON_GetObjectItem(json, "cmdId");
        cJSON *jcmd   = cJSON_GetObjectItem(json, "cmd");

        if (jcmdid && jcmd) {
          //printf("Platform cmdid: %d\n", jcmdid->valueint);
          switch (jcmdid->valueint) {

          case 1: {
            cJSON *jurl = cJSON_GetObjectItem(jcmd,  "pushUrl");
            if (jurl) {
              strcpy(cmdbuf, jurl->valuestring);
              cmdid = jcmdid->valueint;
            }
          } break;

          case 6: {
            cJSON *jlevel = cJSON_GetObjectItem(jcmd,  "level");
            if (jlevel) {
              sprintf(cmdbuf, "%d", jlevel->valueint);
              cmdid = jcmdid->valueint;
            }
          } break;

          default:
            break;
          }
        }
      }
    }

    cJSON_Delete(json);
  }

  return cmdid;
}


int g_rtmpState = 0;
static pthread_t pid;

#define QUEUE_BUFSIZE  (160*1024)
extern void SAMPLE_VENC_1080P_CLASSIC(void *arg);
uint8_t bufRecv[QUEUE_BUFSIZE] = {0};
uint8_t bufSend[QUEUE_BUFSIZE] = {0};


int func_RtmpScheduleStop(void)
{
  SAMPLE_VENC_1080P_CLASSIC_STOP();
  pthread_join(pid, 0);
  iotRtmp_Disconnect();
  queue_deinit();
  g_rtmpState = 0;
}

int func_RtmpSchedule(int cmdid, uint8_t cmdbuf[])
{
  int rc = 0;
  uint32_t rtmpTime;
  static int mode = 0;

  switch (g_rtmpState) {

  /* unconnect */
  case 0: {
    if (cmdid == 1) {

      queue_init(QUEUE_BUFSIZE);
      iotRtmp_Connect(cmdbuf, 10);

      g_rtmpState = 1;
    }
  } break;

  case 1: {
    int width, height, framerate = 30;
    if (mode == 0) {
      width = 1280; height = 720;
    } else if (mode == 1) {
      width = 640; height = 480;
    } else if (mode == 2) {
      width = 320; height = 240;
    }
    rc = iotRtmp_SendMetadata(width, height, framerate);
    if (rc == FALSE) {
      printf("iotRtmp_SendMetadata failed!!!\n");
    }
    if (!queue_empty())
      queue_flush();

    pthread_create(&pid, NULL, SAMPLE_VENC_1080P_CLASSIC, (void*)&mode);

    rtmpTime  = RTMP_GetTime();

    g_rtmpState = 2;
  } break;

  /* connected, sending rtmp packet */
  case 2: {

    if (cmdid == 0) {

      int len = queue_recv(bufRecv, QUEUE_BUFSIZE);
      if (len) {
        rc = iotRtmp_SendH264Packet(bufRecv, len, RTMP_GetTime()-rtmpTime);
        if (rc == FALSE) {
          printf("iotRtmp_SendH264Packet failed!!!\n");
          g_rtmpState = 3;
        }
      }
    } else if (cmdid == 1) {

      /* TODO */
      printf("Get RTMP Reconnect while Playing!!!\n");

    } else if (cmdid == 6) {

#if 1
      int level = cmdbuf[0] - '0';
      if (level == 1) mode = 2;
      else if (level == 2) mode = 1;
      else if (level >= 3) mode = 0;

      SAMPLE_VENC_1080P_CLASSIC_STOP();
      pthread_join(pid, 0);
      g_rtmpState = 1;
#endif
    }

  } break;

  /* disconnect */
  case 3: {

    SAMPLE_VENC_1080P_CLASSIC_STOP();
    pthread_join(pid, 0);

    iotRtmp_Disconnect();
    queue_deinit();
    g_rtmpState = 0;
  } break;

  default:
    break;
  }

  return 0;
}



HI_S32 iotQueue_PutData(VENC_STREAM_S *pstStream)
{
  int i, len;
  uint8_t *buf = bufSend;

  if (queue_full()) {
    printf("Queue is Full!\n");
    return 0;
  }

  for (i = 0, len = 0; i < pstStream->u32PackCount; i++) {
    len += pstStream->pstPack[i].u32Len-pstStream->pstPack[i].u32Offset;
  }
  if ((len <= 0) || (len > QUEUE_BUFSIZE)) {
    printf("Queue the data is invalid: %d !!!\n", len);
    return 0;
  }

  for (i = 0; i < pstStream->u32PackCount; i++) {
    memcpy(buf, pstStream->pstPack[i].pu8Addr+pstStream->pstPack[i].u32Offset, pstStream->pstPack[i].u32Len-pstStream->pstPack[i].u32Offset);
    buf += pstStream->pstPack[i].u32Len-pstStream->pstPack[i].u32Offset;
  }

  queue_send(bufSend, len);

  return 0;
}

