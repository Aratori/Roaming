#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "common/wpa_ctrl.h"

#define MAX_NETWORK_COUNT 64
#define MAX_SSID_LEN 32
#define BUF_SIZE 2048
#define DB_STEP 10

typedef struct 
{
    char ssid[32];
    short sl;
} pair_ssid_sl;

int checkInput()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

int request(struct wpa_ctrl* ctrl, const char* command, char* buf, size_t len)
{
    int command_len = 0;

    for(; command[command_len]; command_len++)
        ;
    return wpa_ctrl_request(ctrl, command, command_len, buf, &len, NULL);
}



//Get ssids and signal level of others AP
//return struct with signal level and ssid of AP
int setScanResults(struct wpa_ctrl* ctrl, pair_ssid_sl* scan_res)
{
    int scan_networks_count = 0;
    char* buf[BUF_SIZE];
    char* point;
    size_t len = BUF_SIZE;
    int counter = 0;
    char num[5];
    int i = 0;
    if(request(ctrl, "SCAN_RESULTS", buf, len) == -1)
    {
        return -1;
    }

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
        memset(&res.ssid[0], 0, MAX_SSID_LEN);

        for(i = 0; point[i] != '\t' ; i++)
            num[i] = point[i];
        res.sl = atoi(num);
        point += i+1;

        //skip flags
        while(*point != '\t')
            point++;
        point++;

        //get ssid
        for(counter = 0; (point[counter] != '\n' && point[counter]!= '\0' && point[counter]!= '\t') ; counter++)//get ssid
            res.ssid[counter] = point[counter];
        memcpy(&scan_res[scan_networks_count], &res, sizeof(pair_ssid_sl));

        scan_networks_count++;
        point = strtok(NULL, "\n");
    }


    return scan_networks_count;
}

//Get ssids of configured networks
//return count of configured networks
int setNetworks(struct wpa_ctrl* ctrl, char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN])
{
    char* buf[BUF_SIZE];
    char* point;
    int counter = 0;
    int network_id_count = 0;
    request(ctrl, "LIST_NETWORKS", buf, BUF_SIZE);

    //parsing
    point = strtok(buf, "\n");  //first line
    while(point != NULL)//new line
    {
        point = strtok(NULL, "\t");//skip id
        if(point == NULL)
            break;

        char ssid[MAX_SSID_LEN] = {0};
        point += 2;  
        for(counter = 0; (point[counter] != '\n' && point[counter]!= '\0' && point[counter]!= '\t'); counter++)//get ssid
            ssid[counter] = point[counter];

        strcpy(&ssids[network_id_count], &ssid);
        point = strtok(NULL, "\n");//skip line
        network_id_count++;
    }
    return network_id_count;
}

//Looking for optimal network
//return index of optimal network
int sortNetworks(const char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN], const pair_ssid_sl* scan_res, int nCount, int sCount, int* newSL)
{
    int sorted[MAX_NETWORK_COUNT] = {0};
    int i, j, index, maxSL, counter;

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
    index = (MAX_NETWORK_COUNT + 1);
    for(i = 0; i < nCount; i++)
    {
        if(sorted[i] == 0)
            continue;
        if(abs(sorted[i]) < maxSL)
        {
            index = i;
            maxSL = abs(sorted[i]);
            *newSL = maxSL;
        }
    }

    return index;
}

int runRoaming()
{
    const char* path = "/var/run/wpa_supplicant/wlan0";
    struct wpa_ctrl* connectionControl;
    char buf[BUF_SIZE];
    char networks[MAX_NETWORK_COUNT][MAX_SSID_LEN]; 
    int nCount = 0;
    int sCount = 0;
    int currentIndex = MAX_NETWORK_COUNT + 1;
    int currentSL = 1000;
    int newIndex = 0;
    int newSL = 0;
    pair_ssid_sl results[MAX_NETWORK_COUNT];
    if((connectionControl = wpa_ctrl_open(path)) == NULL)
    {
        printf("\nFail to run wpa_supplicant\n");
        wpa_ctrl_close(connectionControl);
        exit(0);
    }

    while(!checkInput())
    {
        memset(&results[0], 0, sizeof(results[0])*MAX_NETWORK_COUNT);
        memset(&networks[0][0], 0, MAX_NETWORK_COUNT*MAX_SSID_LEN);;
        
        if(request(connectionControl, "SCAN", buf, BUF_SIZE) == -1)
        {
            wpa_ctrl_close(connectionControl);
            exit(1);
        }
        nCount = setNetworks(connectionControl, networks);
        sCount = setScanResults(connectionControl, &results);
        newIndex = sortNetworks(networks, results, nCount, sCount, &newSL);


        if(currentIndex != newIndex && newIndex < (MAX_NETWORK_COUNT + 1) && abs(newSL - currentSL) > DB_STEP)
        {  
            printf("Selected network: ");
            printf("%d\n", newIndex);

            char commandConnect[20] = {0};
            char commandDisconnect[20] = {0};
            snprintf(commandConnect, sizeof(commandConnect), "SELECT_NETWORK %d", newIndex);
            snprintf(commandDisconnect, sizeof(commandDisconnect), "DISCONNECT %d", currentIndex);

            currentIndex = newIndex;
            currentSL = newSL;
            if(request(connectionControl, commandDisconnect, buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connectionControl);
                exit(2);
            }  
            if(request(connectionControl, commandConnect, buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connectionControl);
                exit(3);
            }        
            if(request(connectionControl, "RECONNECT", buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connectionControl);
                exit(4);
            } 
        }
    }

    wpa_ctrl_close(connectionControl);

    return 0;
}

int main()
{
    runRoaming();

    return 0;
}
