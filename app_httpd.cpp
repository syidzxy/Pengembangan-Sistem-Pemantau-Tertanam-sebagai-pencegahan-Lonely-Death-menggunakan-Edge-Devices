#include <Arduino.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// Global variables
static char current_status[4] = "non";
static uint8_t timer = 0;
static bool gpio_initialized = false;
static bool buzzer_active = false;

// GPIO pins
//const gpio_num_t led_pins[4] = {GPIO_NUM_32, GPIO_NUM_14, GPIO_NUM_33, GPIO_NUM_12};
//const gpio_num_t buzzer_pin = GPIO_NUM_13;

const byte ledPins[4] = {32, 33, 14, 12};
const byte buzzerPin = 13;

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

typedef struct
{
    size_t size;  //number of values used for filtering
    size_t index; //current value index
    size_t count; //value count
    int sum;
    int *values; //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values)
    {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size)
    {
        filter->count++;
    }
    return filter->sum / filter->count;
}
#endif

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_start = esp_timer_get_time();
#endif

    fb = esp_camera_fb_get();
    if (!fb)
    {
        log_e("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char ts[32];
    snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
    httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        size_t fb_len = 0;
#endif
        if (fb->format == PIXFORMAT_JPEG)
        {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            fb_len = fb->len;
#endif
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else
        {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            fb_len = jchunk.len;
#endif
        }
        esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        int64_t fr_end = esp_timer_get_time();
#endif
        log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
        return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            log_e("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
            if (fb->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted)
                {
                    log_e("JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            log_e("Send frame failed");
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
#endif
        log_i("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)"
                 ,
                 (uint32_t)(_jpg_buf_len),
                 (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                 avg_frame_time, 1000.0 / avg_frame_time
        );
    }
    return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char variable[32];
    char value[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
        httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    log_i("%s = %d", variable, val);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize")) {
        if (s->pixformat == PIXFORMAT_JPEG) {
            res = s->set_framesize(s, (framesize_t)val);
        }
    }
    else if (!strcmp(variable, "quality"))
        res = s->set_quality(s, val);
    else if (!strcmp(variable, "contrast"))
        res = s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness"))
        res = s->set_brightness(s, val);
    else if (!strcmp(variable, "saturation"))
        res = s->set_saturation(s, val);
    // else if (!strcmp(variable, "gainceiling"))
    //     res = s->set_gainceiling(s, (gainceiling_t)val);
    // else if (!strcmp(variable, "colorbar"))
    //     res = s->set_colorbar(s, val);
    // else if (!strcmp(variable, "awb"))
    //     res = s->set_whitebal(s, val);
    // else if (!strcmp(variable, "agc"))
    //     res = s->set_gain_ctrl(s, val);
    // else if (!strcmp(variable, "aec"))
    //     res = s->set_exposure_ctrl(s, val);
    // else if (!strcmp(variable, "hmirror"))
    //     res = s->set_hmirror(s, val);
    // else if (!strcmp(variable, "vflip"))
    //     res = s->set_vflip(s, val);
    // else if (!strcmp(variable, "awb_gain"))
    //     res = s->set_awb_gain(s, val);
    // else if (!strcmp(variable, "agc_gain"))
    //     res = s->set_agc_gain(s, val);
    // else if (!strcmp(variable, "aec_value"))
    //     res = s->set_aec_value(s, val);
    // else if (!strcmp(variable, "aec2"))
    //     res = s->set_aec2(s, val);
    // else if (!strcmp(variable, "dcw"))
    //     res = s->set_dcw(s, val);
    // else if (!strcmp(variable, "bpc"))
    //     res = s->set_bpc(s, val);
    // else if (!strcmp(variable, "wpc"))
    //     res = s->set_wpc(s, val);
    // else if (!strcmp(variable, "raw_gma"))
    //     res = s->set_raw_gma(s, val);
    // else if (!strcmp(variable, "lenc"))
    //     res = s->set_lenc(s, val);
    // else if (!strcmp(variable, "special_effect"))
    //     res = s->set_special_effect(s, val);
    // else if (!strcmp(variable, "wb_mode"))
    //     res = s->set_wb_mode(s, val);
    // else if (!strcmp(variable, "ae_level"))
    //     res = s->set_ae_level(s, val);
    else {
        log_i("Unknown command: %s", variable);
        res = -1;
    }

    if (res < 0) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char * p, sensor_t * s, uint16_t reg, uint32_t mask){
    return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];

    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    if(s->id.PID == OV5640_PID || s->id.PID == OV3660_PID){
        for(int reg = 0x3400; reg < 0x3406; reg+=2){
            p+=print_reg(p, s, reg, 0xFFF);//12 bit
        }
        p+=print_reg(p, s, 0x3406, 0xFF);

        p+=print_reg(p, s, 0x3500, 0xFFFF0);//16 bit
        p+=print_reg(p, s, 0x3503, 0xFF);
        p+=print_reg(p, s, 0x350a, 0x3FF);//10 bit
        p+=print_reg(p, s, 0x350c, 0xFFFF);//16 bit

        for(int reg = 0x5480; reg <= 0x5490; reg++){
            p+=print_reg(p, s, reg, 0xFF);
        }

        for(int reg = 0x5380; reg <= 0x538b; reg++){
            p+=print_reg(p, s, reg, 0xFF);
        }

        for(int reg = 0x5580; reg < 0x558a; reg++){
            p+=print_reg(p, s, reg, 0xFF);
        }
        p+=print_reg(p, s, 0x558a, 0x1FF);//9 bit
    } else if(s->id.PID == OV2640_PID){
        p+=print_reg(p, s, 0xd3, 0xFF);
        p+=print_reg(p, s, 0x111, 0xFF);
        p+=print_reg(p, s, 0x132, 0xFF);
    }

    p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
    p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
    p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p += sprintf(p, "\"quality\":%u,", s->status.quality);
    p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", s->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", s->status.aec);
    p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", s->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
    p += sprintf(p, ",\"led_intensity\":%d", -1);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
    } else {
        log_e("Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}

static esp_err_t classify_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Extract status parameter
    char *status = strstr(buf, "status=");
    if (!status) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing 'status' parameter", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    status += 7; // Skip "status=" to get the actual value

    // Update the current status if it has changed
    if (strcmp(status, current_status) != 0) {
        strncpy(current_status, status, sizeof(current_status) - 1);
        current_status[sizeof(current_status) - 1] = '\0';

        // Reset timer and buzzer state
        timer = 0;
        buzzer_active = false;

        // Turn off all LEDs
        for (int i = 0; i < 4; i++) {
            digitalWrite(ledPins[i], LOW);
        }

        Serial.print("Updated classification status: ");
        Serial.println(current_status);
    }

    httpd_resp_send(req, "Classification received", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t timer_handler(httpd_req_t *req) {
    // Initialize GPIOs if not already done
    Serial.print("Status: ");
    Serial.println(current_status);
    Serial.println(sizeof(current_status)/sizeof(char));
        for (int i = 0; i < 4; i++) {

            digitalWrite(ledPins[i], LOW); // Start LEDs OFF
        }
        digitalWrite(buzzerPin, LOW); // Start buzzer OFF
    
    // Handle the "sleeping" status
    if (strcmp(current_status, "TDR") == 0) {
        if (timer < 16) {
            timer++; // Increment the timer
            Serial.print("Timer ON :");
            Serial.println(timer);
            Serial.print("status :");
            Serial.println(current_status);
        }

        // Update LEDs based on the timer
        for (int i = 0; i < 4; i++) {
            if (timer >= (i + 1) * 4) {
                digitalWrite(ledPins[i], HIGH); // Turn on LED
            }
        }

        // Trigger the buzzer when the timer reaches 16
        if (timer == 16 && !buzzer_active) {
            buzzer_active = true;
            Serial.println("Buzzer On");
            digitalWrite(buzzerPin, HIGH); // Turn on buzzer
            delay(3000); // Penundaan 3 detik
            digitalWrite(buzzerPin, 0); // Turn off buzzer
            log_i("Buzzer Off");
        }
    } else {
        // Reset everything if the status is not "sleeping"
        timer = 0;
        buzzer_active = false;
        for (int i = 0; i < 4; i++) {
            digitalWrite(ledPins[i], LOW); // Turn off LEDs
        }
        Serial.println("Human not sleep!!");
    }


    // Respond to the HTTP request
    httpd_resp_send(req, "Timer handler executed", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t test_led_handler(httpd_req_t *req)
{
    for(int i = 0; i < 5; i++){
        for(int pin : ledPins) {
            digitalWrite(pin, HIGH);
        }
        delay(500);
        for(int pin : ledPins) {
            digitalWrite(pin, LOW);
        }
        delay(500);
    }
    return httpd_resp_send(req, "Turned on and off leds 5 times", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t test_buzzer_handler(httpd_req_t *req)
{
    for(int i = 0; i < 5; i++){
        digitalWrite(buzzerPin, HIGH);
        delay(1000);
        digitalWrite(buzzerPin, LOW);
        delay(1000);
    }
    return httpd_resp_send(req, "Turned on and off Buzzer", HTTPD_RESP_USE_STRLEN);
}


/*
static esp_err_t buzzer_handler(httpd_req_t *req) {
    char buf[10];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1); // Read the request data
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Null-terminate the string

    // Parse the "state" parameter
    char *state = strstr(buf, "state=");
    if (!state) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    state += 6; // Skip "state=" to get the actual value

    // Update the buzzer state
    const gpio_num_t buzzer_pin = GPIO_NUM_13;
    if (strcmp(state, "on") == 0) {
        gpio_set_level(buzzer_pin, 1); // Turn the buzzer ON
        httpd_resp_send(req, "Buzzer ON", HTTPD_RESP_USE_STRLEN);
    } else if (strcmp(state, "off") == 0) {
        gpio_set_level(buzzer_pin, 0); // Turn the buzzer OFF
        httpd_resp_send(req, "Buzzer OFF", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_500(req); // Invalid state
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
static esp_err_t classify_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0){
      //httpd_resp_send_400(req);
      httpd_resp_set_status(req, "400 bad request");
      httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
      return ESP_FAIL;
    }

    //extract status parameter from request
    char *status = strstr(buf, "status=");
    if (!status){
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_send(req, "Missing 'status' parameter", HTTPD_RESP_USE_STRLEN);
      return ESP_FAIL;
      //status += 7;
      //process the status
      //log_i("Received classification status: %s", status);
    }
    
    status += 7;
    log_i("Received classification status: %s", status);
    
    //gpio pin config
    const gpio_num_t led_pins[4] = {GPIO_NUM_32, GPIO_NUM_14, GPIO_NUM_33, GPIO_NUM_12};
    const gpio_num_t buzzer_pin = GPIO_NUM_13;
    static uint8_t timer = 0;
    static char current_status[20] = "none";
    static bool gpio_initialized = false;
    static bool buzzer_active = false;
    
    //gpio init
    if (!gpio_initialized){
      gpio_initialized = true;
      for (int i = 0; i < 4; i++){
        gpio_reset_pin(led_pins[i]);
        gpio_set_direction(led_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(led_pins[i], 0); //start led off
      }
      gpio_reset_pin(buzzer_pin);
      gpio_set_direction(buzzer_pin, GPIO_MODE_OUTPUT);
      gpio_set_level(buzzer_pin, 0); //start buzzer off
    }

    //check classification if changed
    if (strcmp(status, current_status) != 0){
      strncpy(current_status, status, sizeof(current_status) - 1);
      current_status[sizeof(current_status) - 1] = '\0';
      timer = 0;
      buzzer_active = false;
      for (int i = 0; i < 4; i++){
        gpio_set_level(led_pins[i], 0);
      }
    }
    //process status
    if (strcmp(current_status, "TIDUR") == 0){
      if (timer < 16) {
        timer++; //timer increment
      }
      //led biner update
      for (int i = 0; i < 4; i++){
        if (timer >= (i + 1) * 4){
          gpio_set_level(led_pins[i], 1);
        }
        //int bit = (timer >> i) & 1; //extract the i-th bit
        //gpio_set_level(led_pins[i], bit); //set led based on the bit
      }
      //buzzer trigger when timer reaches 16 seconds
      if (timer == 16 && !buzzer_active){
        buzzer_active = true;
        log_i("Buzzer On");
        gpio_set_level(buzzer_pin, 1); //buzzer on
        vTaskDelay(pdMS_TO_TICKS(3000)); //3 second delay
        gpio_set_level(buzzer_pin, 0); //buzzer off
        log_i("Buzzer Off");
        //buzzer_active = false;
      }
    }else{
      //reset timer & led off because clasification changes
      timer = 0;
      buzzer_active = false; //buzzer turn off
      for (int i =0; i < 4; i++) {
        gpio_set_level(led_pins[i], 0); //led turn off
      }
      //buzzer_active = false; //buzzer turn off
    }

  httpd_resp_send(req, "Classification received", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}
*/

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

    httpd_uri_t classify_uri = {
        .uri = "/classify",
        .method = HTTP_POST,
        .handler = classify_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
        httpd_uri_t timer_uri = {
        .uri = "/timer",
        .method = HTTP_GET,
        .handler = timer_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
     httpd_uri_t led_uri = {
        .uri = "/test_led",
        .method = HTTP_GET,
        .handler = test_led_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
    httpd_uri_t buzzer_uri = {
        .uri = "/test_buzzer",
        .method = HTTP_GET,
        .handler = test_buzzer_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };

/*
    httpd_uri_t leds_uri = {
      .uri       = "/leds",
      .method    = HTTP_GET,
      .handler   = leds_handler,
      .user_ctx  = NULL
#ifdef CONFIG_HTTPS_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subrpotocol = NULL
#endif
    };    
    httpd_uri_t buzzer_uri = {
      .uri       = "/buzzer",
      .method    = HTTP_GET,
      .handler   = buzzer_handler,
      .user_ctx  = NULL
#ifdef CONFIG_HTTPS_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subrpotocol = NULL
#endif
    };   
*/
    ra_filter_init(&ra_filter, 20);

    log_i("Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &classify_uri);
        httpd_register_uri_handler(camera_httpd, &timer_uri);
        //httpd_register_uri_handler(camera_httpd, &buzzer_uri);
        httpd_register_uri_handler(camera_httpd, &led_uri);
        httpd_register_uri_handler(camera_httpd, &buzzer_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    log_i("Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
