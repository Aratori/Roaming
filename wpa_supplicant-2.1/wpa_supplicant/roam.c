#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "common/wpa_ctrl.h"

#define MAX_NETWORK_COUNT 64
#define MAX_SSID_LEN 32

typedef struct 
{
    char ssid[32];
    short sl;
} pair_ssid_sl;

int flag = 0;

void request(struct wpa_ctrl* ctrl, char* command, char* buf, size_t len)
{
    int command_len = 0;

    for(; command[command_len]; command_len++)
        ;
    wpa_ctrl_request(ctrl, command, command_len, buf, &len, NULL);
}



//Get ssids and signal level of others AP
//return struct with signal level and ssid of AP
int setScanResults(struct wpa_ctrl* ctrl, pair_ssid_sl* scan_res)
{
    int scan_networks_count = 0;
    char* buf[2048];
    char* point;
    size_t len = 2048;
    int counter = 0;
    char num[5];
    int i = 0;

    request(ctrl, "SCAN", buf, len);
    request(ctrl, "SCAN_RESULTS", buf, len);

    //parsing
    strtok(buf, "\n");
    point = strtok(NULL, "\n");  //skip first line
    while(point)//new line
    {   
        for( i = 0 ; i < 2; i++)//skip bssid and freq
        {
            while(*point != '\t')
                point++;
            point++;
        }

        //get signal level
        pair_ssid_sl res;
        for(i = 0; i < 32; i++)
            res.ssid[i] = 0;

        for(i = 0; point[i] != '\t' ; i++)
            num[i] = point[i];
        res.sl = atoi(num);
        point += i+1;

        //skip flags
        while(*point != '\t')
            point++;
        point++;

        //get ssid
        for(counter = 0; ((point[counter] > 40 && point[counter] < 123) || point[counter] == ' ') && (point[counter] != '\n' && point[counter]!= '\0') ; counter++)//get ssid
            res.ssid[counter] = point[counter];
        memcpy(&scan_res[scan_networks_count], &res, sizeof(pair_ssid_sl));

        /*printf("%d ", scan_res[scan_networks_count].sl);
        for(i = 0; i < 32; i++)
           printf("%c", scan_res[scan_networks_count].ssid[i]);
        printf("\n");*/

        scan_networks_count++;
        point = strtok(NULL, "\n");
    }


    return scan_networks_count;
}

//Get ssids of configured networks
//return count of configured networks
int setNetworks(struct wpa_ctrl* ctrl, char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN])
{
    char* buf[1024];
    char* point;
    size_t len = 1000;
    int counter = 0;
    int network_id_count = 0;
    int i = 0;
    request(ctrl, "LIST_NETWORKS", buf, len);

    //parsing
    point = strtok(buf, "\n");  //first line
    while(point != NULL)//new line
    {
        point = strtok(NULL, "\t");//skip id
        if(point == NULL)
            break;

        char ssid[32] = {0};
        point ++;
        point++;   
        for(counter = 0; (point[counter] > 40 && point[counter] < 123) || point[counter] == ' '; counter++)//get ssid
            ssid[counter] = point[counter];
        /*for(i = 0; i < 32; i++)
            printf("%c", ssid[i]);
        printf("\n");*/


        strcpy(&ssids[network_id_count], &ssid);
        point = strtok(NULL, "\n");//skip that line
        network_id_count++;
    }
    return network_id_count;
}

//Looking for optimal network. Less dB - better connection
//return index of optimal network
int sortNetworks(char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN], pair_ssid_sl* scan_res, int nCount, int sCount)
{
    int sorted[MAX_NETWORK_COUNT] = {0};
    int i, j, index, maxSL, counter, size;

    for( i = 0; i < nCount; i++)
    {
        for( j = 0 ; j < sCount ; j++)
        {
            for(counter = 0; counter < MAX_SSID_LEN; counter++)
                if(ssids[i][counter] != scan_res[j].ssid[counter])
                    break;
            if(counter == MAX_SSID_LEN)
            {
                sorted[i] = scan_res[j].sl;
            }
        }
    }

    maxSL = 1000;
    index = 0;
    for(i = 0; i < nCount; i++)
    {
        if(sorted[i] == 0)
            continue;
        if(abs(sorted[i]) < maxSL)
        {
            index = i;
            maxSL = abs(sorted[i]);
        }
    }

    return index;
}

int runRoaming()
{
    const char* path = "/var/run/wpa_supplicant/wlan0";
    struct wpa_ctrl* connectionControl;
    char buf[2048];
    char networks[MAX_NETWORK_COUNT][MAX_SSID_LEN]; 
    int nCount = 0;
    int sCount = 0;
    int currentIndex = 0;
    int newIndex = 0;
    int counter = 0;
    int i, j;
    printf("%d ", (int)'0');
    pair_ssid_sl results[MAX_NETWORK_COUNT];
    if((connectionControl = wpa_ctrl_open(path)) == NULL)
    {
        printf("fail");
    }

    request(connectionControl, "SCAN", buf, 2048);
    request(connectionControl, "SCAN_RESULTS", buf, 2048);
    for(i = 0; buf[i]; i++)
    {
        printf("%c", buf[i]);
    }

    printf("'\0\n");
    /*request(connectionControl, "LIST_NETWORKS", buf, 2048);
    for(i = 0; buf[i]; i++)
    {
        printf("%c", buf[i]);
    }
    printf("\n");*/
    while(counter < 100000000)
    {
        nCount = 0;
        sCount = 0;
        newIndex = 0;
        for(i = 0; i < MAX_NETWORK_COUNT; i++)
        {
            results[i].sl = 0;
            for(j = 0; j < 32; j++)
            {
                results[i].ssid[j] = 0;
                networks[i][j] = 0;
            }
        }
        nCount = setNetworks(connectionControl, networks);
        sCount = setScanResults(connectionControl, &results);
        newIndex = sortNetworks(networks, results, nCount, sCount);

        if(newIndex == 0)
        {
            counter++;
            continue;
        }

        if(currentIndex != newIndex)
        {  
            printf("\nSelected network: ");
            printf("%d", newIndex);

            char commandSet[20] = {0};
            char num[10] = {0};
            snprintf(num, sizeof(num), "%d", newIndex);
            strcpy(commandSet, "SELECT_NETWORK ");//15
            strcat(commandSet, num);
            printf("\n%d", newIndex);
            for(i = 0; commandSet[i]; i++)
                printf("%c", commandSet[i]);
            printf("\n");
            currentIndex = newIndex;
            request(connectionControl, commandSet, buf, 2048);
            request(connectionControl, "RECONNECT", buf, 2048);
        }
        counter++;
    }

    wpa_ctrl_close(connectionControl);

    return 0;
}

int main()
{
    runRoaming();

    return 0;
}
