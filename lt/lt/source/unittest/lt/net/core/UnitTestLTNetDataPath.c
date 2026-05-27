/*******************************************************************************
 *
 * UnitTestLTNetDataPath.c
 * -----------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>

#include <lt/net/core/LTMemoryPool.h>
#include <lt/net/core/LTNetBuffer.h>
#include <lt/net/core/LTNetQueue.h>
#include <lt/net/core/LTNetHandle.h>
#include <lt/net/core/LTNetDataPath.h>
#include <lt/core/LTMonitor.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <tilt/JiltEngine.h>

static struct Statics {
    Tilt                *tilt;
    LTCore              *core;
    LTMemoryPool        *pMemoryPool;
    LTNetBuffer         *pNetBuffer;
    void                *pBuffer;
    LTNetDataPath       *ltNetDp;
    LTThread            rxTHread1;
    LTThread            rxTHread2;
    LTThread            rxTHread3;
    LTNet_buff          *pktArray[100];
    LTMonitor           *monitor;
} S;

static JiltEngine *s_engine;

#define IS_LT_SIZE_ALIGNED(ptr) (((u32)(ptr) % sizeof(LT_SIZE)) == 0)

void MemoryPoolCreateTest(Tilt *tilt) {
    S.pMemoryPool = lt_createobject(LTMemoryPool);
    TILT_EXPECT_TRUE(tilt, S.pMemoryPool != NULL, "Memory Pool object is NULL");
    S.pBuffer = NULL;
    if (S.pMemoryPool) {
        S.pBuffer = lt_malloc(1620*10);
        TILT_EXPECT_TRUE(tilt, S.pMemoryPool->API->Create(S.pMemoryPool, 1620, 10, S.pBuffer, 1620*10)==true, "Memory Pool Create");
        u32 next_addr = (u32)S.pBuffer;
        for (int i = 0 ; i < 9; i++) {
            next_addr = next_addr + 1620;
            TILT_EXPECT_TRUE(tilt, *(u32*)((u32)S.pBuffer + i*1620)==next_addr, "Memory Pool Create error");
        }
        TILT_EXPECT_TRUE(tilt, *(u32*)((u32)S.pBuffer + 9*1620)==0xFFFFFFFF, "Memory Pool Create error");
        void *block[10] = {NULL};
        for (int i =0; i < 10; i++) {
            TILT_EXPECT_TRUE(tilt, S.pMemoryPool->API->Alloc(S.pMemoryPool, &block[i])==true, "Memory Pool Alloc Error");
            TILT_EXPECT_TRUE(tilt, block[i]!=NULL, "Memory Pool Alloc");
            TILT_EXPECT_TRUE(tilt, IS_LT_SIZE_ALIGNED(block[i])==true, "Memory is not LT_SIZE aligned");
        }
        void *fail_block = NULL;
        TILT_EXPECT_TRUE(tilt, S.pMemoryPool->API->Alloc(S.pMemoryPool, &fail_block)==false, "Memory Pool Alloc Error");

        for (int i=9; i >= 0; i--) {
            S.pMemoryPool->API->Free(S.pMemoryPool, block[i]);
            block[i] = NULL;
        }
        int i = 0;
        next_addr = (u32)S.pBuffer;
        while (next_addr != 0xFFFFFFFF) {
            next_addr = *(u32*)next_addr;
            i++;
        }
        TILT_EXPECT_TRUE(tilt, i==10, "Memory Pool Free Error");
        lt_free(S.pBuffer);
        S.pMemoryPool->API->Destroy(S.pMemoryPool);
        lt_destroyobject(S.pMemoryPool);
        S.pMemoryPool = NULL;
    }
}

typedef struct Ethernet_hdr {
    u8  dest[6];
    u8  src[6];
    u16 type;
} __attribute__((packed))Ether_hdr;

typedef struct IP_hdr {
    u8  ver_ihl;
    u8  tos;
    u16 len;
    u16 id;
    u16 flags_fo;
    u8  ttl;
    u8  proto;
    u16 chksum;
    u32 src;
    u32 dest;
} __attribute__((packed))IP_hdr;

typedef struct UDP_hdr {
    u16 src;
    u16 dest;
    u16 len;
    u16 chksum;
} __attribute__((packed))UDP_hdr;

static void TestBasicWithVariousSize(Tilt *tilt, u32 size) {
    const u32 BUFF_SIZE = size;
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer!=NULL, "LTNetBuffer object is NULL");

    LTNet_buff *net_buff = S.pNetBuffer->Alloc(BUFF_SIZE);
    TILT_EXPECT_TRUE(tilt, net_buff!=NULL, "LTNetBuffer Alloc Error");
    TILT_EXPECT_TRUE(tilt, net_buff->next==NULL, "LTNetBuffer Alloc Error, net_buff->next");
    TILT_EXPECT_TRUE(tilt, net_buff->prev==NULL, "LTNetBuffer Alloc Error, net_buff->prev");
    TILT_EXPECT_TRUE(tilt, net_buff->len==0, "LTNetBuffer Alloc Error, net_buff->len");
    TILT_EXPECT_TRUE(tilt, net_buff->head==net_buff->data, "LTNetBuffer Alloc Error, net_buff->head");
    TILT_EXPECT_TRUE(tilt, net_buff->tail==0, "LTNetBuffer Alloc Error, net_buff->tail");
    TILT_EXPECT_TRUE(tilt, net_buff->end==BUFF_SIZE, "LTNetBuffer Alloc Error, net_buff->end");
    TILT_EXPECT_TRUE(tilt, LTAtomic_Load(&net_buff->users)==1, "LTNetBuffer Alloc Error, net_buff->users");

    S.pNetBuffer->Reserve(net_buff, sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+10);
    TILT_EXPECT_TRUE(tilt, net_buff->data==net_buff->head+sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Reserve Error, net_buff->data");
    TILT_EXPECT_TRUE(tilt, net_buff->tail==sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Reserve Error, net_buff->tail");

    void *pdata = S.pNetBuffer->Push(net_buff, 10);
    TILT_EXPECT_TRUE(tilt, pdata==net_buff->data, "LTNetBuffer Push Error");
    TILT_EXPECT_TRUE(tilt, net_buff->data==net_buff->head+sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr), "LTNetBuffer Push Error, net_buff->data");
    TILT_EXPECT_TRUE(tilt, net_buff->len==10, "LTNetBuffer Push Error, net_buff->len");

    UDP_hdr *udp_data = (UDP_hdr*)S.pNetBuffer->Push(net_buff, sizeof(UDP_hdr));
    TILT_EXPECT_TRUE(tilt, udp_data==(UDP_hdr*)(net_buff->data), "LTNetBuffer Push Error");
    TILT_EXPECT_TRUE(tilt, net_buff->data==net_buff->head+sizeof(Ether_hdr)+sizeof(IP_hdr), "LTNetBuffer Push Error, net_buff->data");
    TILT_EXPECT_TRUE(tilt, net_buff->len==sizeof(UDP_hdr)+10, "LTNetBuffer Push Error, net_buff->len");

    IP_hdr *ip_data = (IP_hdr*)S.pNetBuffer->Push(net_buff, sizeof(IP_hdr));
    TILT_EXPECT_TRUE(tilt, ip_data==(IP_hdr*)(net_buff->data), "LTNetBuffer Push Error");
    TILT_EXPECT_TRUE(tilt, net_buff->data==net_buff->head+sizeof(Ether_hdr), "LTNetBuffer Push Error, net_buff->data");
    TILT_EXPECT_TRUE(tilt, net_buff->len==sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Push Error, net_buff->len");

    Ether_hdr *ether_data = (Ether_hdr*)S.pNetBuffer->Push(net_buff, sizeof(Ether_hdr));
    TILT_EXPECT_TRUE(tilt, ether_data==(Ether_hdr*)(net_buff->data), "LTNetBuffer Push Error");
    TILT_EXPECT_TRUE(tilt, net_buff->data==net_buff->head, "LTNetBuffer Push Error, net_buff->data");
    TILT_EXPECT_TRUE(tilt, net_buff->len==sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Push Error, net_buff->len");

    lt_memset((u8*)pdata, 0xab, 10);
    udp_data->src = 0x1234;
    udp_data->dest = 0x5678;
    udp_data->len = 10;
    udp_data->chksum = 0x1111;

    ip_data->ver_ihl = 0x45;
    ip_data->tos = 0x00;
    ip_data->len = sizeof(IP_hdr)+sizeof(UDP_hdr)+10;
    ip_data->id = 0x1234;
    ip_data->flags_fo = 0x0000;
    ip_data->ttl = 0x40;
    ip_data->proto = 0x11;
    ip_data->chksum = 0x1111;
    ip_data->src = 0x12345678;
    ip_data->dest = 0x87654321;

    ether_data->dest[0] = 0x00;
    ether_data->dest[1] = 0x11;
    ether_data->dest[2] = 0x22;
    ether_data->dest[3] = 0x33;
    ether_data->dest[4] = 0x44;
    ether_data->dest[5] = 0x55;

    ether_data->src[0] = 0x55;
    ether_data->src[1] = 0x44;
    ether_data->src[2] = 0x33;
    ether_data->src[3] = 0x22;
    ether_data->src[4] = 0x11;
    ether_data->src[5] = 0x00;
    void *add_data = S.pNetBuffer->Put(net_buff, 10);
    lt_memset(add_data, 0xab, 10);

    LTNet_buff *new_net_buff = S.pNetBuffer->Copy(net_buff);
    TILT_EXPECT_TRUE(tilt, new_net_buff!=NULL, "LTNetBuffer Copy Error");
    TILT_EXPECT_TRUE(tilt, new_net_buff->next==NULL, "LTNetBuffer Copy Error, net_buff->next");
    TILT_EXPECT_TRUE(tilt, new_net_buff->prev==NULL, "LTNetBuffer Copy Error, net_buff->prev");
    TILT_EXPECT_TRUE(tilt, new_net_buff->len==sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+20, "LTNetBuffer Copy Error, net_buff->len");
    TILT_EXPECT_TRUE(tilt, new_net_buff->head==new_net_buff->data, "LTNetBuffer Copy Error, net_buff->head");
    TILT_EXPECT_TRUE(tilt, new_net_buff->tail==sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr)+20, "LTNetBuffer Copy Error, net_buff->tail");
    TILT_EXPECT_TRUE(tilt, new_net_buff->end==BUFF_SIZE, "LTNetBuffer Copy Error, net_buff->end");
    TILT_EXPECT_TRUE(tilt, LTAtomic_Load(&new_net_buff->users)==1, "LTNetBuffer Copy Error, net_buff->users");

    Ether_hdr *new_ether_data = (Ether_hdr*)new_net_buff->data;
    IP_hdr *new_ip_data = (IP_hdr*)(new_net_buff->data+sizeof(Ether_hdr));
    UDP_hdr *new_udp_data = (UDP_hdr*)(new_net_buff->data+sizeof(Ether_hdr)+sizeof(IP_hdr));
    u8 *new_pdata = new_net_buff->data+sizeof(Ether_hdr)+sizeof(IP_hdr)+sizeof(UDP_hdr);
    TILT_EXPECT_TRUE(tilt, lt_memcmp(new_pdata, pdata, 20)==0, "LTNetBuffer Copy Error, Data");
    TILT_EXPECT_TRUE(tilt, new_udp_data->src==0x1234, "LTNetBuffer Copy Error, UDP src");
    TILT_EXPECT_TRUE(tilt, new_udp_data->dest==0x5678, "LTNetBuffer Copy Error, UDP dest");
    TILT_EXPECT_TRUE(tilt, new_udp_data->len==10, "LTNetBuffer Copy Error, UDP len");
    TILT_EXPECT_TRUE(tilt, new_udp_data->chksum==0x1111, "LTNetBuffer Copy Error, UDP chksum");

    TILT_EXPECT_TRUE(tilt, new_ip_data->ver_ihl==0x45, "LTNetBuffer Copy Error, IP ver_ihl");
    TILT_EXPECT_TRUE(tilt, new_ip_data->tos==0x00, "LTNetBuffer Copy Error, IP tos");
    TILT_EXPECT_TRUE(tilt, new_ip_data->len==sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Copy Error, IP len");
    TILT_EXPECT_TRUE(tilt, new_ip_data->id==0x1234, "LTNetBuffer Copy Error, IP id");
    TILT_EXPECT_TRUE(tilt, new_ip_data->flags_fo==0x0000, "LTNetBuffer Copy Error, IP flags_fo");
    TILT_EXPECT_TRUE(tilt, new_ip_data->ttl==0x40, "LTNetBuffer Copy Error, IP ttl");
    TILT_EXPECT_TRUE(tilt, new_ip_data->proto==0x11, "LTNetBuffer Copy Error, IP proto");
    TILT_EXPECT_TRUE(tilt, new_ip_data->chksum==0x1111, "LTNetBuffer Copy Error, IP chksum");
    TILT_EXPECT_TRUE(tilt, new_ip_data->src==0x12345678, "LTNetBuffer Copy Error, IP src");
    TILT_EXPECT_TRUE(tilt, new_ip_data->dest==0x87654321, "LTNetBuffer Copy Error, IP dest");

    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[0]==0x00, "LTNetBuffer Copy Error, Ether dest[0]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[1]==0x11, "LTNetBuffer Copy Error, Ether dest[1]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[2]==0x22, "LTNetBuffer Copy Error, Ether dest[2]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[3]==0x33, "LTNetBuffer Copy Error, Ether dest[3]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[4]==0x44, "LTNetBuffer Copy Error, Ether dest[4]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->dest[5]==0x55, "LTNetBuffer Copy Error, Ether dest[5]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[0]==0x55, "LTNetBuffer Copy Error, Ether src[0]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[1]==0x44, "LTNetBuffer Copy Error, Ether src[1]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[2]==0x33, "LTNetBuffer Copy Error, Ether src[2]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[3]==0x22, "LTNetBuffer Copy Error, Ether src[3]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[4]==0x11, "LTNetBuffer Copy Error, Ether src[4]");
    TILT_EXPECT_TRUE(tilt, new_ether_data->src[5]==0x00, "LTNetBuffer Copy Error, Ether src[5]");

    Ether_hdr *ehdr = (Ether_hdr*)S.pNetBuffer->Pull(new_net_buff, 0);
    TILT_EXPECT_TRUE(tilt, ehdr->dest[0]==0x00, "LTNetBuffer Pull Error, Ether dest[0]");
    TILT_EXPECT_TRUE(tilt, ehdr->dest[1]==0x11, "LTNetBuffer Pull Error, Ether dest[1]");
    TILT_EXPECT_TRUE(tilt, ehdr->dest[2]==0x22, "LTNetBuffer Pull Error, Ether dest[2]");
    TILT_EXPECT_TRUE(tilt, ehdr->dest[3]==0x33, "LTNetBuffer Pull Error, Ether dest[3]");
    TILT_EXPECT_TRUE(tilt, ehdr->dest[4]==0x44, "LTNetBuffer Pull Error, Ether dest[4]");
    TILT_EXPECT_TRUE(tilt, ehdr->dest[5]==0x55, "LTNetBuffer Pull Error, Ether dest[5]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[0]==0x55, "LTNetBuffer Pull Error, Ether src[0]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[1]==0x44, "LTNetBuffer Pull Error, Ether src[1]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[2]==0x33, "LTNetBuffer Pull Error, Ether src[2]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[3]==0x22, "LTNetBuffer Pull Error, Ether src[3]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[4]==0x11, "LTNetBuffer Pull Error, Ether src[4]");
    TILT_EXPECT_TRUE(tilt, ehdr->src[5]==0x00, "LTNetBuffer Pull Error, Ether src[5]");

    IP_hdr *ihdr = (IP_hdr*)S.pNetBuffer->Pull(new_net_buff, sizeof(Ether_hdr));
    TILT_EXPECT_TRUE(tilt, ihdr->ver_ihl==0x45, "LTNetBuffer Pull Error, IP ver_ihl");
    TILT_EXPECT_TRUE(tilt, ihdr->tos==0x00, "LTNetBuffer Pull Error, IP tos");
    TILT_EXPECT_TRUE(tilt, ihdr->len==sizeof(IP_hdr)+sizeof(UDP_hdr)+10, "LTNetBuffer Pull Error, IP len");
    TILT_EXPECT_TRUE(tilt, ihdr->id==0x1234, "LTNetBuffer Pull Error, IP id");
    TILT_EXPECT_TRUE(tilt, ihdr->flags_fo==0x0000, "LTNetBuffer Pull Error, IP flags_fo");
    TILT_EXPECT_TRUE(tilt, ihdr->ttl==0x40, "LTNetBuffer Pull Error, IP ttl");
    TILT_EXPECT_TRUE(tilt, ihdr->proto==0x11, "LTNetBuffer Pull Error, IP proto");
    TILT_EXPECT_TRUE(tilt, ihdr->chksum==0x1111, "LTNetBuffer Pull Error, IP chksum");
    TILT_EXPECT_TRUE(tilt, ihdr->src==0x12345678, "LTNetBuffer Pull Error, IP src");
    TILT_EXPECT_TRUE(tilt, ihdr->dest==0x87654321, "LTNetBuffer Pull Error, IP dest");

    UDP_hdr *uhdr = (UDP_hdr*)S.pNetBuffer->Pull(new_net_buff, sizeof(IP_hdr));
    TILT_EXPECT_TRUE(tilt, uhdr->src==0x1234, "LTNetBuffer Pull Error, UDP src");
    TILT_EXPECT_TRUE(tilt, uhdr->dest==0x5678, "LTNetBuffer Pull Error, UDP dest");
    TILT_EXPECT_TRUE(tilt, uhdr->len==10, "LTNetBuffer Pull Error, UDP len");
    TILT_EXPECT_TRUE(tilt, uhdr->chksum==0x1111, "LTNetBuffer Pull Error, UDP chksum");

    u8 *pdata1 = (u8*)S.pNetBuffer->Pull(new_net_buff, sizeof(UDP_hdr));
    TILT_EXPECT_TRUE(tilt, lt_memcmp(pdata1, pdata, 20)==0, "LTNetBuffer Pull Error, Data");


    S.pNetBuffer->Free(new_net_buff);
    LTNet_buff* new_net_buff1 = S.pNetBuffer->Get(net_buff);
    TILT_EXPECT_TRUE(tilt, new_net_buff1==net_buff, "LTNetBuffer Get Error");
    TILT_EXPECT_TRUE(tilt, LTAtomic_Load(&net_buff->users)==2, "LTNetBuffer Get Error, net_buff->users");
    S.pNetBuffer->Free(net_buff);
    TILT_EXPECT_TRUE(tilt, LTAtomic_Load(&new_net_buff1->users)==1, "LTNetBuffer Free Error, net_buff->users");
    S.pNetBuffer->Free(new_net_buff1);


    lt_closelibrary(S.pNetBuffer);
}

void LTNetBufferBasicTest(Tilt *tilt) {
    TestBasicWithVariousSize(tilt, 231);
    TestBasicWithVariousSize(tilt, 491);
    TestBasicWithVariousSize(tilt, 1010);
}

void LTNetBufferScaleTest(Tilt *tilt) {
    const u32 BUFF_SIZE = 1024*1024;
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer!=NULL, "LTNetBuffer object is NULL");

    LTNet_buff *net_buff = S.pNetBuffer->Alloc(BUFF_SIZE);
    TILT_EXPECT_TRUE(tilt, net_buff==NULL, "LTNetBuffer Alloc Error, it should return NULL");

    net_buff = S.pNetBuffer->Alloc(0);
    TILT_EXPECT_TRUE(tilt, net_buff!=NULL, "LTNetBuffer Alloc Error, it should support zero-size LTNetBuff");
    TILT_EXPECT_TRUE(tilt, net_buff->head==NULL, "LTNetBuffer Alloc Error, the data should be set to NULL");
    S.pNetBuffer->Free(net_buff);

    int block_cnt = 99;
    LTNet_buff* net_buff_array[block_cnt];
    for (int i = 0; i <block_cnt; i++) {
        net_buff_array[i] = S.pNetBuffer->Alloc(0);
        char str[100];
        lt_snprintf(str, sizeof(str), "LTNetBuffer Alloc zero size Error i = %d", i);
        TILT_EXPECT_TRUE(tilt, net_buff_array[i]!=NULL, str);
    }
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer->Alloc(0)==NULL, "LTNetBuffer Alloc Error, it should return NULL as it exceeds the limit");
    for (int i = 0; i <block_cnt; i++) {
        S.pNetBuffer->Free(net_buff_array[i]);
    }

    block_cnt = 10;
    for (int i = 0; i <block_cnt; i++) {
        net_buff_array[i] = S.pNetBuffer->Alloc(231);
        TILT_EXPECT_TRUE(tilt, net_buff_array[i]!=NULL, "LTNetBuffer Alloc Error");
    }
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer->Alloc(231)==NULL, "LTNetBuffer Alloc Error, it should return NULL as it exceeds the limit");
    for (int i = 0; i <block_cnt; i++) {
        S.pNetBuffer->Free(net_buff_array[i]);
    }

    for (int i = 0; i <block_cnt; i++) {
        net_buff_array[i] = S.pNetBuffer->Alloc(491);
        TILT_EXPECT_TRUE(tilt, net_buff_array[i]!=NULL, "LTNetBuffer Alloc Error");
    }
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer->Alloc(491)==NULL, "LTNetBuffer Alloc Error, it should return NULL as it exceeds the limit");
    for (int i = 0; i <block_cnt; i++) {
        S.pNetBuffer->Free(net_buff_array[i]);
    }
    block_cnt = 9;
    for (int i = 0; i <block_cnt; i++) {
        net_buff_array[i] = S.pNetBuffer->Alloc(1010);
        TILT_EXPECT_TRUE(tilt, net_buff_array[i]!=NULL, "LTNetBuffer Alloc Error");
    }
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer->Alloc(1010)==NULL, "LTNetBuffer Alloc Error, it should return NULL as it exceeds the limit");
    for (int i = 0; i <block_cnt; i++) {
        S.pNetBuffer->Free(net_buff_array[i]);
    }
    lt_closelibrary(S.pNetBuffer);
}

void LTNetQueueCreateTest(Tilt *tilt) {
    LTNetQueue* queue = lt_createobject(LTNetQueue);
    TILT_EXPECT_TRUE(tilt, queue!=NULL, "LTNetQueue object is NULL");
    TILT_EXPECT_TRUE(tilt, queue->API->Create(queue, 5)==true, "LTNetQueue Create Error");
    LTNet_buff *buff_array[7];
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    TILT_EXPECT_TRUE(tilt, S.pNetBuffer!=NULL, "LTNetBuffer object is NULL");
    for (int i = 0; i < 7; i++) {
        buff_array[i] = S.pNetBuffer->Alloc(231);
        TILT_EXPECT_TRUE(tilt, buff_array[i]!=NULL, "LTNetBuffer Alloc Error");
    }

    for (int i = 0; i < 5; i++) {
        TILT_EXPECT_TRUE(tilt, queue->API->Push(queue, buff_array[i])==true, "LTNetQueue Push Error");
    }
    TILT_EXPECT_TRUE(tilt, queue->API->Push(queue, buff_array[5])==false, "LTNetQueue Push Error, it should return false as it exceeds the limit");
    TILT_EXPECT_TRUE(tilt, queue->API->Push(queue, buff_array[6])==false, "LTNetQueue Push Error, it should return false as it exceeds the limit");

    for (int i=0; i < 5; i++) {
        LTNet_buff *buff = queue->API->Pop(queue);
        TILT_EXPECT_TRUE(tilt, buff_array[i] == buff, "LTNetQueue Pop Error");
    }

    for (int i = 0; i < 7; i++) {
        S.pNetBuffer->Free(buff_array[i]);
    }
    lt_closelibrary(S.pNetBuffer);

    queue->API->Destroy(queue);
    lt_destroyobject(queue);
}

static bool result[6] = {false, false, false, false, false};
static LTAtomic pop_cnt;

static void queue_pop_func(void *arg) {
    LTNetQueue *queue = (LTNetQueue*)arg;
    LTNet_buff *buff = queue->API->Pop(queue);
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetQueue Pop Error");
    int cnt = LTAtomic_FetchAdd(&pop_cnt, 1);
    result[cnt] = true;
    S.pNetBuffer->Free(buff);
}

void DispatchCallbackComplete(LTThread_ReleaseReason reason, void *arg) {
    LT_UNUSED(arg);
    LT_UNUSED(reason);
}

static void func(void *arg) {
    LTNetQueue *queue = (LTNetQueue*)arg;
    queue->API->RegisterNotification(queue, &queue_pop_func, &DispatchCallbackComplete, queue, false);
}

static void un_func(void) {
    LTNetQueue *queue = (LTNetQueue*)lt_getlibraryinterface(ILTThread, S.core)->GetThreadSpecificClientData(lt_getlibraryinterface(ILTThread, S.core)->GetCurrentThread(), "queue");
    queue->API->UnRegisterNotification(queue, &queue_pop_func);
}

void LTNetQueueMultipleThreadTest(Tilt *tilt) {
    LTNetQueue* queue = lt_createobject(LTNetQueue);
    LTAtomic_Store(&pop_cnt, 0);
    lt_memset(result, 0x00, sizeof(result));
    TILT_EXPECT_TRUE(tilt, queue!=NULL, "LTNetQueue object is NULL");
    TILT_EXPECT_TRUE(tilt, queue->API->Create(queue, 6)==true, "LTNetQueue Create Error");
    int cnt = 1;
    LT_UNUSED(cnt);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    LTThread tids[6] = {0};
    for (int i = 0; i < 6; i++) {
        char name[32];
        lt_snprintf(name, sizeof(name), "LTNQMltThTst_%d", i);
        tids[i] = S.core->CreateThread(name);
        iThread->SetThreadSpecificClientData(tids[i], "queue", NULL, queue);
        iThread->SetStackSize(tids[i], 1024);
        iThread->Start(tids[i], NULL, un_func);
        iThread->QueueTaskProc(tids[i], func, NULL, queue);
    }
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    for (int i =0; i < 6; i++) {
        LTNet_buff* buff = S.pNetBuffer->Alloc(231);
        TILT_EXPECT_TRUE(tilt, queue->API->Push(queue, buff)==true, "LTNetQueue Push Error");
    }
    iThread->Sleep(LTTime_Milliseconds(10));
    queue->API->Notify(queue);
    iThread->Sleep(LTTime_Milliseconds(1000));
    TILT_EXPECT_TRUE(tilt, queue->API->Size(queue)==0, "LTNetQueue Size Error");
    for (int i = 0; i < 6; i++) {
        iThread->Terminate(tids[i]);
        iThread->WaitUntilFinished(tids[i], LTTime_Infinite());
        lt_destroyhandle(tids[i]);
    }
    lt_closelibrary(S.pNetBuffer);
    queue->API->Destroy(queue);
    lt_destroyobject(queue);
    for (int i = 0; i < 6; i++) {
        TILT_EXPECT_TRUE(tilt, result[i]==true, "LTNetQueue Multiple Thread Test Error");
    }
    lt_memset(result, 0x00, sizeof(result));
    LTAtomic_Store(&pop_cnt, 0);
}

static LTNet_buff *packet_list[91] = {NULL};
static LTMonitor *monitor = NULL;

int poll_cb(void *data, int budget) {
    int *cnt = (int*)data;
    *cnt = *cnt+1;
    static int i = 0;
    if(i >= 91) i = 0;
    int work_done = 0;
    for (int j = i; (j < i+budget)&&(j<91); j++) {
        packet_list[j] = S.pNetBuffer->Alloc(0);
        TILT_EXPECT_TRUE(S.tilt, packet_list[j]!=NULL, "LTNetBuffer Alloc Error");
        work_done++;
    }
    i += work_done;
    if (work_done < budget) {
        monitor->API->Enter(monitor);
        monitor->API->Notify(monitor);
        monitor->API->Exit(monitor);
    }
    return work_done;
}

void notify_proc(void *arg) {
    LTNetHandle *handle = (LTNetHandle*)arg;
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    for (int i=0; i < 10; i++) {
        handle->API->Notify(handle);
        iThread->Yield();
    }
}

void LTNetHandleTest(Tilt *tilt) {
    LTNetHandle *handle = lt_createobject(LTNetHandle);
    lt_memset(packet_list, 0x00, sizeof(packet_list));
    TILT_EXPECT_TRUE(tilt, handle!=NULL, "LTNetHandle object is NULL");
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    monitor = lt_createobject(LTMonitor);
    handle->API->Enable(handle);
    int cnt = 0;
    TILT_EXPECT_TRUE(tilt, handle->API->Add(handle, kLTThread_PriorityHighest-10, poll_cb, &cnt)==true, "LTNetHandle Create Error");

    // Create a thread to send notification
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    LTThread tid = S.core->CreateThread("LTNetHandleNotifyTest");
    iThread->SetPriority(tid, kLTThread_PriorityHighest);
    iThread->Start(tid, NULL, NULL);
    iThread->QueueTaskProc(tid, notify_proc, NULL, handle);

    monitor->API->Enter(monitor);
    monitor->API->Wait(monitor, LTTime_Infinite());
    monitor->API->Exit(monitor);
    iThread->Terminate(tid);
    iThread->WaitUntilFinished(tid, LTTime_Infinite());
    TILT_EXPECT_TRUE(tilt, cnt==10, "LTNetHandle Poll Error");
    lt_destroyhandle(tid);
    for (int i = 0 ; i < 91; i++) {
        TILT_EXPECT_TRUE(tilt, packet_list[i]!=NULL, "LTNetHandle Poll Error");
        if(packet_list[i]) S.pNetBuffer->Free(packet_list[i]);
    }
    TILT_EXPECT_TRUE(tilt, handle->API->Remove(handle, poll_cb)==true, "LTNetHandle Remove Error");
    handle->API->Disable(handle);
    lt_destroyobject(handle);
    lt_closelibrary(S.pNetBuffer);
    lt_destroyobject(monitor);
    monitor = NULL;
}

static bool buff_tx_comp_done = false;

static bool lt_buf_comp_cb(LTNet_buff *buff, void *ctx) {
    TILT_EXPECT_TRUE(S.tilt, buff==(LTNet_buff*)ctx, "LTNetDataPath Transmit Error");
    S.pNetBuffer->Free(buff);
    buff_tx_comp_done = true;
    return true;
}

static bool transmit_called = false;

static bool unittest_transmit(LTNet_buff *buff, void *ctx, LTNetBufferCompletionCb cb) {
    LTNet_buff *sptr = (LTNet_buff*)ctx;
    TILT_EXPECT_TRUE(S.tilt, buff==sptr, "LTNetDataPath Transmit Error");
    transmit_called = true;
    TILT_EXPECT_TRUE(S.tilt, cb(buff, ctx)==true , "LTNetDataPath Transmit Error");
    return true;
}

static int net_dp_poll_cb(void *data, int budget) {
    LTNetDataPath *net_dp = (LTNetDataPath*)data;
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    LTNet_buff *buff = S.pNetBuffer->Alloc(100);
    S.pNetBuffer->Reserve(buff, sizeof(LTNetDataPathPktHdr));
    LTNetDataPathPktHdr* metadata = S.pNetBuffer->Push(buff, sizeof(LTNetDataPathPktHdr));
    metadata->queue_type = kLTNetDataPathQueueType_WlanRespQueue;
    TILT_EXPECT_TRUE(S.tilt, net_dp->Receive(buff, true)==true, "LTNetDataPath Receive Error");
    lt_closelibrary(S.pNetBuffer);
    return budget-1;
}

static bool rx_cb(LTNetDataPathQueueType queue_type, LTNet_buff *buff, void *ctx) {
    LT_UNUSED(ctx);
    LT_UNUSED(queue_type);
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    S.pNetBuffer->Free(buff);
    return true;
}

void LTNetDataPathTest(Tilt *tilt) {
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    LTNetDataPath* ltNetDp = lt_openlibrary(LTNetDataPath);
    TILT_EXPECT_TRUE(tilt, ltNetDp!=NULL, "LTNetDataPath object is NULL");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_WlanRespQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_CommonRespQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_NetworkRespQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_SocketRespQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_BleRespQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_WlanEventQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_NetworkEventQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_SocketEventQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->SubscribeToQueue(kLTNetDataPathQueueType_BleEventQueue, rx_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    LTNetHandle *netHandle = lt_createobject(LTNetHandle);

    netHandle->API->Add(netHandle, kLTThread_PriorityHighest-10, net_dp_poll_cb, (void*)ltNetDp);
    netHandle->API->Enable(netHandle);

    LTNet_buff *buff = S.pNetBuffer->Alloc(100);
    S.pNetBuffer->SetBufferCompletionCb(buff, lt_buf_comp_cb, NULL);
    ltNetDp->SetSendTxCallback(unittest_transmit, buff);
    TILT_EXPECT_TRUE(tilt, ltNetDp->Transmit(kLTNetDataPathQueueType_CommonCmdQueue, buff, true) == 0, "LTNetDataPath Transmit Error");

    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->Sleep(LTTime_Milliseconds(1000));
    TILT_EXPECT_TRUE(tilt, transmit_called==true, "LTNetDataPath Transmit Error");
    TILT_EXPECT_TRUE(tilt, buff_tx_comp_done==true, "LTNetDataPath Transmit Error");

    netHandle->API->Notify(netHandle);
    iThread->Sleep(LTTime_Milliseconds(1000));

    LTNetDataPathStats stats = {0};
    TILT_EXPECT_TRUE(tilt, ltNetDp->GetStatistics(&stats), "LTNetDataPath GetStatistics Error");
    for (int i = 0; i < kLTNetDataPathQueueType_Max; i++) {
        if (i == kLTNetDataPathQueueType_CommonRespQueue) {
            TILT_EXPECT_TRUE(tilt, stats.stats[i].size == 1, "LTNetDataPath GetStatistics Error");
        } else {
            TILT_EXPECT_TRUE(tilt, stats.stats[i].size == 0, "LTNetDataPath GetStatistics Error");
        }
    }

    netHandle->API->Remove(netHandle, net_dp_poll_cb);
    netHandle->API->Disable(netHandle);

    lt_closelibrary(S.pNetBuffer);
    lt_destroyobject(netHandle);

    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_WlanRespQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_CommonRespQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_NetworkRespQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_SocketRespQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_BleRespQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_WlanEventQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_NetworkEventQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_SocketEventQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
    TILT_EXPECT_TRUE(tilt, ltNetDp->UnSubscribeFromQueue(kLTNetDataPathQueueType_BleEventQueue, rx_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");

    lt_closelibrary(ltNetDp);
    transmit_called = false;
    buff_tx_comp_done = false;
}

static void received_pkt_is_wlan_cb(void *context) {
    LTNet_buff *buff = (LTNet_buff*)context;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    u8* data = (u8*)buff->data;
    S.pNetBuffer->Pull(buff, sizeof(u8));
    TILT_EXPECT_TRUE(S.tilt, (*data%3)==1, "LTNetDataPath Receive wlan pkt Error");
    buff->compl_cb(buff, NULL);
}

static void received_pkt_is_network_cb(void *context) {
    LTNet_buff *buff = (LTNet_buff*)context;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    u8* data = (u8*)buff->data;
    S.pNetBuffer->Pull(buff, sizeof(u8));
    TILT_EXPECT_TRUE(S.tilt, (*data%3)==2, "LTNetDataPath Receive Network pkt Error");
    buff->compl_cb(buff, NULL);
}

static void received_pkt_is_socket_cb(void *context) {
    LTNet_buff *buff = (LTNet_buff*)context;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    u8* data = (u8*)buff->data;
    S.pNetBuffer->Pull(buff, sizeof(u8));
    TILT_EXPECT_TRUE(S.tilt, (*data%3)==0, "LTNetDataPath Receive Network pkt Error");
    buff->compl_cb(buff, NULL);
}

static bool received_wlan_evt_pkt_cb(LTNetDataPathQueueType queue_type, LTNet_buff *buff, void *context) {
    LT_UNUSED(context);
    if (queue_type != kLTNetDataPathQueueType_WlanEventQueue) return false;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    //Notify the thread1 to process the packet
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->QueueTaskProc(S.rxTHread1, received_pkt_is_wlan_cb, NULL, buff);
    return true;
}

static bool received_network_evt_pkt_cb(LTNetDataPathQueueType queue_type, LTNet_buff *buff, void *context) {
    LT_UNUSED(context);
    if (queue_type != kLTNetDataPathQueueType_NetworkEventQueue) return false;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    //Notify the thread1 to process the packet
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->QueueTaskProc(S.rxTHread2, received_pkt_is_network_cb, NULL, buff);
    return true;
}

static bool received_socket_evt_pkt_cb(LTNetDataPathQueueType queue_type, LTNet_buff *buff, void *context) {
    LT_UNUSED(context);
    if (queue_type != kLTNetDataPathQueueType_SocketEventQueue) return false;
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Receive Error");
    //Notify the thread1 to process the packet
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->QueueTaskProc(S.rxTHread3, received_pkt_is_socket_cb, NULL, buff);
    return true;
}

static bool register_to_rx(void) {
    void *pClientData = lt_getlibraryinterface(ILTThread, S.core)->GetThreadSpecificClientData(lt_getlibraryinterface(ILTThread, S.core)->GetCurrentThread(), "qtr");
    LTNetDataPathQueueType type = *(LTNetDataPathQueueType*)pClientData;
    if(type == kLTNetDataPathQueueType_WlanEventQueue) {
        TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->SubscribeToQueue(type, received_wlan_evt_pkt_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    } else if(type == kLTNetDataPathQueueType_NetworkEventQueue) {
        TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->SubscribeToQueue(type, received_network_evt_pkt_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    } else if(type == kLTNetDataPathQueueType_SocketEventQueue) {
        TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->SubscribeToQueue(type, received_socket_evt_pkt_cb, NULL)==true, "LTNetDataPath SubscribeToQueue Error");
    }
    return true;
}

static void unregister_from_wlan_rx(void) {
    LTNetDataPathQueueType type = kLTNetDataPathQueueType_WlanEventQueue;
    TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->UnSubscribeFromQueue(type, received_wlan_evt_pkt_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
}

static void unregister_from_net_rx(void) {
    LTNetDataPathQueueType type = kLTNetDataPathQueueType_NetworkEventQueue;
    TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->UnSubscribeFromQueue(type, received_network_evt_pkt_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
}

static void unregister_from_sock_rx(void) {
    LTNetDataPathQueueType type = kLTNetDataPathQueueType_SocketEventQueue;
    TILT_EXPECT_TRUE(S.tilt, S.ltNetDp->UnSubscribeFromQueue(type, received_socket_evt_pkt_cb, NULL)==true, "LTNetDataPath UnSubscribeToQueue Error");
}

static void free_meta_data(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    lt_free(pClientData);
}

static u32 pkt_cnt = 0;
static bool tx_buffer_cb(LTNet_buff *buff, void *ctx, LTNetBufferCompletionCb cb) {
    LTNetHandle * netHandle = (LTNetHandle*)ctx;
    LT_UNUSED(cb);
    TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Transmit Error");
    //Save the packets and do LTNetHandle::notify after receiving 10 packets

    S.pktArray[pkt_cnt++] = buff;
    if ((pkt_cnt+1) % 10 == 0) {
        netHandle->API->Notify(netHandle);
    }

    return true;
}

static LTAtomic free_buff;
static bool buffer_comp_cb(LTNet_buff *net_buff, void *ctx) {
    LT_UNUSED(ctx);
    S.pNetBuffer->Free(net_buff);
    LTAtomic_FetchAdd(&free_buff, 1);
    return true;
}

static int ltnet_poll_cb(void *data, int budget) {
    LT_UNUSED(data);
    for (int i = 0; i < budget; i++) {
        LTNet_buff *buff = S.pktArray[i];
        TILT_EXPECT_TRUE(S.tilt, buff!=NULL, "LTNetDataPath Poll Error");
        S.ltNetDp->Receive(buff, false);
    }
    S.ltNetDp->Receive(NULL, true);
    //Read the 10 packets and ship it to LTNetDataPath
    return 0;
}

static bool send_tx_pkt(void) {
    for (u32 i=1; i <= 100; i++) {
        LTNet_buff *buff = NULL;
        while((buff = S.pNetBuffer->Alloc(100)) == NULL) {
            S.ltNetDp->Transmit(kLTNetDataPathQueueType_CommonCmdQueue, NULL, true);
            lt_getlibraryinterface(ILTThread, S.core)->Yield();
        }
        S.pNetBuffer->SetBufferCompletionCb(buff, buffer_comp_cb, NULL);
        S.pNetBuffer->Reserve(buff, sizeof(LTNetDataPathPktHdr)+sizeof(u8));
        u8* data = (u8*)S.pNetBuffer->Push(buff, sizeof(u8));
        *data = (u8)i;
        LTNetDataPathPktHdr* metadata = S.pNetBuffer->Push(buff, sizeof(LTNetDataPathPktHdr));
        if (i%3 == 1) {
            metadata->queue_type = kLTNetDataPathQueueType_WlanEventQueue;
        } else if (i%3 == 2) {
            metadata->queue_type = kLTNetDataPathQueueType_NetworkEventQueue;
        } else {
            metadata->queue_type = kLTNetDataPathQueueType_SocketEventQueue;
        }
        if (S.ltNetDp->Transmit(kLTNetDataPathQueueType_CommonCmdQueue, buff, false) == -1) {
            S.ltNetDp->Transmit(kLTNetDataPathQueueType_CommonCmdQueue, NULL, true);
            lt_getlibraryinterface(ILTThread, S.core)->Yield();
        }
    }
    S.ltNetDp->Transmit(kLTNetDataPathQueueType_CommonCmdQueue, NULL, true);
    return false;
}

static void LTNetDataPathScaleTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    S.pNetBuffer = lt_openlibrary(LTNetBuffer);
    S.ltNetDp    = lt_openlibrary(LTNetDataPath);
    LTNetHandle *netHandle = lt_createobject(LTNetHandle);
    LTAtomic_Store(&free_buff, 0);

    //The test case is to create one TX thread and 3 RX Threads.
    //The TX thread will send 100 packets but will backoff whenever it gets a failure in allocating
    //packet or failed to enqueue the packet.
    //The TX callback registered with LTNetDataPath will receive the packets and after receiving every 10th
    //packet will notify the LTNetHandle to poll the same 10 packets and will send to the LTNetDataPath.
    // The RX thread will receive the notifications but will process packets in this order
    // thread 1 will process 1, 4, 7, 10, 13, ... packets.
    // thread 2 will process 2, 5, 8, 11, 14, ... packets.
    // thread 3 will process 3, 6, 9, 12, 15, ... packets.
    // The test case will check whether all the packets are received by the RX threads in the order
    // they are sent by the TX thread.

    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    S.rxTHread1 = S.core->CreateThread("LTNetDataPathScaleTest_RX1");
    iThread->SetStackSize(S.rxTHread1, 1024);
    u8* ptr = lt_malloc(sizeof(LTNetDataPathQueueType));
    *ptr = kLTNetDataPathQueueType_WlanEventQueue;
    iThread->SetThreadSpecificClientData(S.rxTHread1, "qtr", free_meta_data, ptr);
    iThread->Start(S.rxTHread1, register_to_rx, unregister_from_wlan_rx);

    S.rxTHread2 = S.core->CreateThread("LTNetDataPathScaleTest_RX2");
    iThread->SetStackSize(S.rxTHread2, 1024);
    ptr = lt_malloc(sizeof(LTNetDataPathQueueType));
    *ptr = kLTNetDataPathQueueType_NetworkEventQueue;
    iThread->SetThreadSpecificClientData(S.rxTHread2, "qtr", free_meta_data, ptr);
    iThread->Start(S.rxTHread2, register_to_rx, unregister_from_net_rx);

    S.rxTHread3 = S.core->CreateThread("LTNetDataPathScaleTest_RX3");
    iThread->SetStackSize(S.rxTHread3, 1024);
    ptr = lt_malloc(sizeof(LTNetDataPathQueueType));
    *ptr = kLTNetDataPathQueueType_SocketEventQueue;
    iThread->SetThreadSpecificClientData(S.rxTHread3, "qtr", free_meta_data, ptr);
    iThread->Start(S.rxTHread3, register_to_rx, unregister_from_sock_rx);

    //Setup LTNetHandle to poll the packets
    netHandle->API->Add(netHandle, kLTThread_PriorityDefault, ltnet_poll_cb, NULL);
    netHandle->API->Enable(netHandle);

    //Register TX Callback to LTNetDataPath
    S.ltNetDp->SetSendTxCallback(tx_buffer_cb, netHandle);

    LTThread txTHread = S.core->CreateThread("LTNetDataPathScaleTest_TX");
    iThread->SetStackSize(txTHread, 1024);
    iThread->Start(txTHread, send_tx_pkt, NULL);

    iThread->Sleep(LTTime_Milliseconds(1000));

    //iThread->Terminate(txTHread);
    //iThread->WaitUntilFinished(txTHread, LTTime_Infinite());
    //lt_destroyhandle(txTHread);

    iThread->Sleep(LTTime_Milliseconds(1000));

    TILT_EXPECT_TRUE(tilt, LTAtomic_Load(&free_buff)==100, "LTNetDataPath Scale Test Error");
    iThread->Terminate(S.rxTHread1);
    iThread->WaitUntilFinished(S.rxTHread1, LTTime_Infinite());
    lt_destroyhandle(S.rxTHread1);

    iThread->Terminate(S.rxTHread2);
    iThread->WaitUntilFinished(S.rxTHread2, LTTime_Infinite());
    lt_destroyhandle(S.rxTHread2);

    iThread->Terminate(S.rxTHread3);
    iThread->WaitUntilFinished(S.rxTHread3, LTTime_Infinite());
    lt_destroyhandle(S.rxTHread3);

    netHandle->API->Remove(netHandle, ltnet_poll_cb);
    netHandle->API->Disable(netHandle);


    lt_destroyobject(netHandle);
    lt_closelibrary(S.core);
    lt_closelibrary(S.pNetBuffer);
    lt_closelibrary(S.ltNetDp);
    lt_destroyhandle(txTHread);
    pkt_cnt = 0;
}

static u32 ap_count = 0;
static void HandleScanAps(LTWiFi_ApInfo *ap, void *clientData) {
    Tilt *tilt = (Tilt*)clientData;
    if (!ap) {
        TILT_ASSERT_TRUE(tilt, ap_count > 0, "no APs found");
        s_engine->API->SignalTestCompletion(s_engine, "LTDeviceWiFiTest");
        return;
    }
    ap_count++;
    return;
}

void LTDeviceWiFiTest(Tilt *tilt) {
    LTDeviceWiFi* wifi = lt_openlibrary(LTDeviceWiFi);
    ap_count = 0;
    TILT_EXPECT_TRUE(tilt, wifi!=NULL, "LTDeviceWiFi object is NULL");
    wifi->ScanAps(NULL, HandleScanAps, (void*)tilt);
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(8), NULL, NULL);
    lt_closelibrary(wifi);
}

static LTSocket sockFd;
static LTNetCore *netCore;
static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void *data) {
    LT_UNUSED(hSocket);
    LT_UNUSED(event);
    LT_UNUSED(data);
    if (event == kLTSocket_Event_WriteReady) {
        char buffer[20];
        lt_snprintf(buffer, sizeof(buffer), "Start");
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockFd);
        if (pSocket->WriteSocket(sockFd, buffer, 5) != 5) {
        }
    } else if (event == kLTSocket_Event_ReadReady) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockFd);
        for (int i = 0; i < 3; i++) {
            s32 len = pSocket->ReadSocket(sockFd, NULL, 0);
            if (len <= 0) break;
            u8 *buffer = lt_malloc(len+1);
            lt_memset(buffer, 0x00, len+1);

            if (pSocket->ReadSocket(sockFd, buffer, len) == -1) {
            } else {
            }
            lt_free(buffer);
        }
        LTNetIpv4Endpoint recv_ep;
        if (!pSocket->GetProperty(sockFd, "recv.endpoint.v4", &recv_ep)) {
        } else {
        }
        S.monitor->API->Enter(S.monitor);
        S.monitor->API->Notify(S.monitor);
        S.monitor->API->Exit(S.monitor);
    }
}

static void OnNetCoreEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    LT_UNUSED(hTransport);
    if (event == kLTTransport_Event_Up) {
        if (sockFd) return; // socket already open
        bool bind_only = false;
        if (!bind_only) {
            sockFd = netCore->OpenSocket(0, "udp ip: 192.168.0.246 port: 1024", OnSocketEvent, clientData);
        } else {
            sockFd = netCore->OpenSocket(0, "udp", OnSocketEvent, clientData);
        }
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockFd);
        LTNetIpv4Endpoint local;
        if (!pSocket->GetProperty(sockFd, "local.endpoint.v4", &local)) {
        } else {
        }

        if(bind_only) {
            LTNetIpv4Endpoint remote;
            remote.address = 0xf600a8c0;
            remote.port = 1024;
            if (!pSocket->SetProperty(sockFd, "send.endpoint.v4", &remote)) {
            } else {
                char buffer[20];
                lt_snprintf(buffer, sizeof(buffer), "Start");
                ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, sockFd);
                if (pSocket->WriteSocket(sockFd, buffer, 5) != 5) {
                } else {
                }
            }
        }
    }
}

bool SocketThreadStart(void) {
    netCore = lt_openlibrary(LTNetCore);
    netCore->OpenTransport(NULL, OnNetCoreEvent, NULL);
    return true;
}
void SocketThreadStop(void) {
    if (sockFd) {
        lt_destroyhandle(sockFd);
        sockFd = 0;
    }
    lt_closelibrary(netCore);
}

static LTThread dns_test;
void LTDeviceNetSocketUdpTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    dns_test = S.core->CreateThread("test");
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->SetStackSize(dns_test, 2*2048);
    iThread->Start(dns_test, SocketThreadStart, SocketThreadStop);
    S.monitor->API->Enter(S.monitor);
    S.monitor->API->Wait(S.monitor, LTTime_Infinite());
    S.monitor->API->Exit(S.monitor);
    iThread->Terminate(dns_test);
    iThread->WaitUntilFinished(dns_test, LTTime_Infinite());
}

static LTSocket tcp_client_sockFd;
static LTNetCore *tcp_cl_netCore;

static void OnSocketTcEvent(LTSocket hSocket, LTSocket_Event event, void *data) {
    LT_UNUSED(hSocket);
    LT_UNUSED(event);
    LT_UNUSED(data);
    if (event == kLTSocket_Event_WriteReady) {
        char buffer[20];
        lt_snprintf(buffer, sizeof(buffer), "Start");
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, tcp_client_sockFd);
        if (pSocket->WriteSocket(tcp_client_sockFd, buffer, 5) != 5) {
        } else {
        }
    } else if (event == kLTSocket_Event_ReadReady) {
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, tcp_client_sockFd);
        s32 len = 50;
        u8 *buffer = lt_malloc(len+1);
        lt_memset(buffer, 0x00, len+1);

        if (pSocket->ReadSocket(tcp_client_sockFd, buffer, len) == -1) {
        } else {
        }
        lt_free(buffer);
        LTNetIpv4Endpoint recv_ep;
        if (!pSocket->GetProperty(tcp_client_sockFd, "recv.endpoint.v4", &recv_ep)) {
        } else {
        }
        S.monitor->API->Enter(S.monitor);
        S.monitor->API->Notify(S.monitor);
        S.monitor->API->Exit(S.monitor);
    }
}

static void OnNetCoreTcEvent(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    LT_UNUSED(hTransport);
    if (event == kLTTransport_Event_Up) {
        if (tcp_client_sockFd) return; // socket already open
        tcp_client_sockFd =  tcp_cl_netCore->OpenSocket(0, "tcp ip: 192.168.0.246 port: 1024", OnSocketTcEvent, clientData);
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, tcp_client_sockFd);
        LTNetIpv4Endpoint local;
        if (!pSocket->GetProperty(tcp_client_sockFd, "local.endpoint.v4", &local)) {
        } else {
        }
    }
}

bool SocketThreadTcStart(void) {
    tcp_cl_netCore = lt_openlibrary(LTNetCore);
    tcp_cl_netCore->OpenTransport(NULL, OnNetCoreTcEvent, NULL);
    return true;
}
void SocketThreadTcStop(void) {
    if (tcp_client_sockFd) {
        lt_destroyhandle(tcp_client_sockFd);
        tcp_client_sockFd = 0;
    }
    lt_closelibrary(tcp_cl_netCore);
}

LTThread tcp_client_test;
void LTDeviceNetSocketTcpClientTest(Tilt *tilt) {
    LT_UNUSED(tilt);
    tcp_client_test = S.core->CreateThread("tcp_client_test");
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    iThread->SetStackSize(tcp_client_test, 2*2048);
    iThread->Start(tcp_client_test, SocketThreadTcStart, SocketThreadTcStop);
    S.monitor->API->Enter(S.monitor);
    S.monitor->API->Wait(S.monitor, LTTime_Infinite());
    S.monitor->API->Exit(S.monitor);
    iThread->Terminate(tcp_client_test);
    iThread->WaitUntilFinished(tcp_client_test, LTTime_Infinite());
    lt_destroyhandle(tcp_client_test);
}

typedef struct {
    Tilt *tilt;
    LTMonitor *monitor;
} LTNetSocketDnsTestObj;

typedef struct {
    LTNetSocketDnsTestObj *pObj;
    int id;
} LTNetSocketDnsTestThreadData;

static LTThread rx_tr;
static LTThread dns_thr[3];
static u8 dns_res_status = 0x0;
static LTNetCore *netCore;

static void SocketDnsRes(void *data) {
    LTNetSocketDnsTestThreadData *tdata = (LTNetSocketDnsTestThreadData*)data;
    LTNetSocketDnsTestObj *pObj = tdata->pObj;
    dns_res_status |= (0x01 << (tdata->id-1));
    if (dns_res_status == 0x7) {
        pObj->monitor->API->Enter(pObj->monitor);
        pObj->monitor->API->Notify(pObj->monitor);
        pObj->monitor->API->Exit(pObj->monitor);
    }
}

static void OnDnsSocketEvent(LTSocket hSocket, LTSocket_Event event, void *clientData) {
    LT_UNUSED(hSocket);
    LT_UNUSED(clientData);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    void *data = iThread->GetThreadSpecificClientData(iThread->GetCurrentThread(), "dns");
    LTNetSocketDnsTestThreadData *tdata = (LTNetSocketDnsTestThreadData*)data;
    if (event == kLTSocket_Event_DnsError) {
        TILT_EXPECT_TRUE(tdata->pObj->tilt, false, "LTNetSocketDnsTest DNS Error");
    } else if (event == kLTSocket_Event_DnsResolved) {
        iThread->QueueTaskProc(rx_tr, SocketDnsRes, NULL, tdata);
    }
}

static bool SocketDnsThreadStart(void) {
    LTSocket sockFd;
    sockFd =  netCore->OpenSocket(0, "dns host: www.google.com", OnDnsSocketEvent, NULL);
    LT_UNUSED(sockFd);
    return true;
}

static void LTNetSocketDnsTestThreadDataRel(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    lt_free(pClientData);
}

static void OnTransportEventDnsTest(LTTransport hTransport, LTTransport_Event event, void *clientData) {
    LT_UNUSED(hTransport);
    if (event == kLTTransport_Event_Up) {
        //Transport Came UP.
        //Create receiver thread
        rx_tr = S.core->CreateThread("rx_tr");
        ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
        iThread->SetStackSize(rx_tr, 2048);
        iThread->Start(rx_tr, NULL, NULL);
        // Now create three separate threads to open three sockets
        for (int i = 1 ; i <= 3; i++) {
            char thr_name[10];
            lt_snprintf(thr_name, sizeof(thr_name), "dns_thr-%d", i);
            dns_thr[i-1] = S.core->CreateThread(thr_name);
            iThread->SetStackSize(dns_thr[i-1], 1024);
            LTNetSocketDnsTestThreadData *tdata = lt_malloc(sizeof(LTNetSocketDnsTestThreadData));
            tdata->pObj = (LTNetSocketDnsTestObj*)clientData;
            tdata->id = i;
            iThread->SetThreadSpecificClientData(dns_thr[i-1], "dns", LTNetSocketDnsTestThreadDataRel, tdata);
            iThread->Start(dns_thr[i-1], SocketDnsThreadStart, NULL);
        }
    }
}

static bool StartDnsTransportThread(void) {
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    LTNetSocketDnsTestObj *pTest = iThread->GetThreadSpecificClientData(iThread->GetCurrentThread(), "dns");
    // Open Transport
    LTTransport hTransport = netCore->OpenTransport(NULL, OnTransportEventDnsTest, pTest);
    //TILT_ASSERT_TRUE(pTest->tilt, hTransport!=LTHANDLE_INVALID, "LTNetSocketDnsTest Transport Open Error");
    LT_UNUSED(hTransport);
    return true;
}

void LTNetSocketDnsTest(Tilt *tilt) {
    netCore = lt_openlibrary(LTNetCore);
    LTNetSocketDnsTestObj *pTest = lt_malloc(sizeof(LTNetSocketDnsTestObj));
    pTest->tilt = (Tilt *)tilt;
    pTest->monitor = lt_createobject(LTMonitor);
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, S.core);
    LTThread dns_test = S.core->CreateThread("dns_test");
    iThread->SetStackSize(dns_test, 2*2048);
    iThread->SetThreadSpecificClientData(dns_test, "dns", NULL, pTest);
    iThread->Start(dns_test, StartDnsTransportThread, NULL);
    pTest->monitor->API->Enter(pTest->monitor);
    if (!pTest->monitor->API->Wait(pTest->monitor, LTTime_Milliseconds(10000))) {
        TILT_EXPECT_TRUE(tilt, false, "LTNetSocketDnsTest Timeout");
    } else {
        TILT_EXPECT_TRUE(tilt, true, "LTNetSocketDnsTest Success");
    }
    pTest->monitor->API->Exit(pTest->monitor);
    iThread->Terminate(dns_test);
    iThread->WaitUntilFinished(dns_test, LTTime_Infinite());
    for (int i = 1 ; i <= 3; i++) {
        iThread->Terminate(dns_thr[i-1]);
        iThread->WaitUntilFinished(dns_thr[i-1], LTTime_Infinite());
    }
    iThread->Terminate(rx_tr);
    iThread->WaitUntilFinished(rx_tr, LTTime_Infinite());
    lt_free(pTest);
    lt_closelibrary(netCore);
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics){};
    S.tilt    = tilt;
    S.core    = LT_GetCore();
    S.monitor = lt_createobject(LTMonitor);
    TILT_EXPECT_TRUE(tilt, S.monitor != NULL, "Cannot create monitor");
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    lt_destroyobject(S.monitor);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

/*******************************************************************************
 * Tilt Test Table
 ******************************************************************************/
static const TiltEngineTest s_tests[] = {
    { MemoryPoolCreateTest,           "MemoryPoolCreateTest",           "Test Create Memory Pool",          0 },
    { LTNetBufferBasicTest,           "LTNetBufferBasicTest",           "Test LTNetBuffer",                 0 },
    { LTNetBufferScaleTest,           "LTNetBufferScaleTest",           "Test LTNetBuffer memory scale",    0 },
    { LTNetQueueCreateTest,           "LTNetQueueCreateTest",           "Test Create LTNetQueue",           0 },
    { LTNetQueueMultipleThreadTest,   "LTNetQueueMultipleThreadTest",   "Test Multiple Thread LTNetQueue",  0 },
    { LTNetHandleTest,                "LTNetHandleTest",                "Test LTNetHandle",                 0 },
    { LTNetDataPathTest,              "LTNetDataPathTest",              "Test LTNetDataPath",               0 },
    { LTNetDataPathScaleTest,         "LTNetDataPathScaleTest",         "Test LTNetDataPath memory scale",  0 },
    { LTDeviceWiFiTest,               "LTDeviceWiFiTest",               "LTDeviceWiFi Test",                0 },
    { LTNetSocketDnsTest,             "LTNetSocketDnsTest",             "Socket Test with DNS Spec",        0 },
    { LTDeviceNetSocketUdpTest,       "LTDeviceNetSocketUdpTest",       "Socket Test with UDP Spec",        0 },
    { LTDeviceNetSocketTcpClientTest, "LTDeviceNetSocketTcpClientTest", "Socket Test with TCP Client Spec", 0 }
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static int UnitTestLTNetDataPathImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetDataPathImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetDataPathImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetDataPath, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetDataPath, UnitTestLTNetDataPathImpl_Run, 1536) LTLIBRARY_DEFINITION;
