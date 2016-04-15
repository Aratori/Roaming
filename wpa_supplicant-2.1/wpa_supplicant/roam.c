#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "common/wpa_ctrl.h"

#define MAX_NETWORK_COUNT 64
#define MAX_SSID_LEN 32
#define BUF_SIZE  4096
#define DB_STEP 10
#define TIME_INTERVAL 2

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
    int command_len = strlen(command);
    return wpa_ctrl_request(ctrl, command, command_len, buf, &len, NULL);
}



//Get ssids and signal level of others AP
//return struct with signal level and ssid of AP
int getScanResults(struct wpa_ctrl* ctrl, pair_ssid_sl* scan_res)
{
    int scan_networks_count = 0;
    char buf[BUF_SIZE] = {0};
    char* point;
    size_t len = BUF_SIZE;
    int counter = 0;
    char num[10] = {0};
    int i = 0;
    if(request(ctrl, "SCAN_RESULTS", buf, BUF_SIZE) == -1)
    {
        return -1;
    }
    //parsing
    strtok(buf, "\n");
    point = strtok(NULL, "\n");  //skip first line
    while(point)//new line
    {  
        point = strchr(point, '\t') + 1;   
        point = strchr(point, '\t') + 1;   
        //get signal level
        pair_ssid_sl res;
        memset(&res.ssid[0], 0, MAX_SSID_LEN);

        for(i = 0; point[i] != '\t' && i < 10; i++)
            num[i] = point[i];
        res.sl = atoi(num);
        point += i+1;
        //skip flags
        point = strchr(point, '\t') + 1;

        //get ssid
        for(counter = 0; (point[counter] != '\n' && point[counter]!= '\0') && counter < MAX_SSID_LEN; counter++)//get ssid
            res.ssid[counter] = point[counter];
        memcpy(&scan_res[scan_networks_count], &res, sizeof(pair_ssid_sl));
        //printf("%s %s\n", num, scan_res[scan_networks_count].ssid);
        //printf("%s \n", scan_res[scan_networks_count].ssid);
        scan_networks_count++;
        point = strtok(NULL, "\n");
    }


    return scan_networks_count;
}

//Get ssids of configured networks
//return count of configured networks
int getNetworks(struct wpa_ctrl* ctrl, char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN])
{
    char buf[BUF_SIZE] = {0};
    char* point = 0;
    int counter = 0;
    int network_id_count = 0;

    if(request(ctrl, "LIST_NETWORKS", buf, BUF_SIZE) == -1)
        return -1;

    //parsing
    strtok(buf, "\n");  //first line
    point = strtok(NULL, "\n");
    while(point)//new line
    {
        char ssid[MAX_SSID_LEN];

        point = strchr(point, '\t') + 1;
        for(counter = 0; point[counter] != '\t' && point[counter] != '\n' && point[counter]!= '\0' && counter < MAX_SSID_LEN; counter++)//get ssid
            ssid[counter] = point[counter];
        ssid[counter] = 0;
        strcpy(&ssids[network_id_count], &ssid);
        //printf("%s \n", ssids[network_id_count]);
        point = strchr(point, '\n');//skip line
        network_id_count++;
    }
    return network_id_count;
}

//Looking for optimal network
//return index of optimal network
int findOptimalNetwork(const char ssids[MAX_NETWORK_COUNT][MAX_SSID_LEN], const pair_ssid_sl* scan_res, int n_count, int s_count, int* new_sl)
{
    int sorted[MAX_NETWORK_COUNT] = {0};
    int i, j, index, max_sl, counter;


    for( i = 0; i < n_count; i++)
    {
        for( j = 0 ; j < s_count ; j++)
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

    max_sl = 1000;
    index = (MAX_NETWORK_COUNT + 1);
    for(i = 0; i < n_count; i++)
    {
        if(sorted[i] == 0)
            continue;
        if(abs(sorted[i]) < max_sl)
        {
            index = i;
            max_sl = abs(sorted[i]);
            *new_sl = max_sl;
        }
    }

    return index;
}

int runRoaming()
{
    const char* path = "/var/run/wpa_supplicant/wlan0";
    struct wpa_ctrl* connection_control;
    char buf[BUF_SIZE];
    char networks[MAX_NETWORK_COUNT][MAX_SSID_LEN]; 
    int n_count = 0; 
    int s_count = 0;
    int cur_index = MAX_NETWORK_COUNT + 1;
    int current_sl = 1000;
    int new_index = 0;
    int new_sl = 0;
    pair_ssid_sl results[MAX_NETWORK_COUNT];
    if((connection_control = wpa_ctrl_open(path)) == NULL)
    {
        printf("\nFail to run wpa_supplicant\n");
        wpa_ctrl_close(connection_control);
        return 0;
    }

    while(!checkInput())
    {
        memset(&results[0], 0, sizeof(results[0])*MAX_NETWORK_COUNT);
        memset(&networks[0][0], 0, MAX_NETWORK_COUNT*MAX_SSID_LEN);;
        
        if(request(connection_control, "SCAN", buf, BUF_SIZE) == -1)
        {
            wpa_ctrl_close(connection_control);
            return 0;
        }
        n_count = getNetworks(connection_control, networks);
        s_count = getScanResults(connection_control, &results);
        new_index = findOptimalNetwork(networks, results, n_count, s_count, &new_sl);


        if(cur_index != new_index && new_index < (MAX_NETWORK_COUNT + 1) && abs(new_sl - current_sl) > DB_STEP)
        {  
            printf("Selected network: ");
            printf("%d\n", new_index);

            char command_connect[20] = {0};
            char command_disconnect[20] = {0};
            snprintf(command_connect, sizeof(command_connect), "SELECT_NETWORK %d", new_index);
            snprintf(command_disconnect, sizeof(command_disconnect), "DISCONNECT %d", cur_index);

            cur_index = new_index;
            current_sl = new_sl;
            if(request(connection_control, command_disconnect, buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connection_control);
                return 0;
            }  
            if(request(connection_control, command_connect, buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connection_control);
                return 0;
            }        
            if(request(connection_control, "RECONNECT", buf, BUF_SIZE) == -1)
            {
                wpa_ctrl_close(connection_control);
                return 0;
            } 
        }
    }

    wpa_ctrl_close(connection_control);

    return 1;
}

int main()
{
    if(!runRoaming())
        exit(0);

    return 0;
}
