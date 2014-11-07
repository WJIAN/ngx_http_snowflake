twitter nowflake is a network service for generating unique ID numbers.
https://github.com/twitter/snowflake/

We use server_id instead of data center id and worker_id is pid.
The http response is json format;

### nginx.conf
    location /id {
        sf_server_id 1;
    
        #Optional 
        sf_epoch 1288834974657;
        sf_sequence_bits 12;
        sf_server_id_bits 5;
        sf_worker_id_bits 5;
    
       snowflake;
    }

### 
    curl http://127.0.0.1/id
    {"id":"530553642621599802","server_id":"1","worker_id":"23066","timestamp":"1415328820268"}
