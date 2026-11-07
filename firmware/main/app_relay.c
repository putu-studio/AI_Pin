#include "app_relay.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "secrets.h"
#include <string.h>
#include "app_out.h"
#include "esp_crt_bundle.h"   // the built-in list of trusted certificate authorities
#include "app_display.h"



esp_err_t app_relay_ask(const char *question)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "text", question);
    char *json_string = cJSON_PrintUnformatted(root);

    esp_http_client_config_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .url = RELAY_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Device-Token", RELAY_TOKEN);

    int len = strlen(json_string);
    esp_err_t err = esp_http_client_open(client, len);
    if (err != ESP_OK){
        out_printf("Can't reach relay. PC on?\n");
        err = ESP_FAIL;
        goto cleanup;
    }
    
    esp_http_client_write(client, json_string, len);
    
    int64_t hdr_len = esp_http_client_fetch_headers(client);
    if (hdr_len < 0){
        out_printf("No reply headers\n");
        err = ESP_FAIL;
        goto cleanup;
    }

    int status = esp_http_client_get_status_code(client);
    if(status != 200){
        out_printf("Relay error %d\n", status);
        err = ESP_FAIL;
        goto cleanup;
    }

    static char body[4096];
    int total =0;
    int n;

    while ((n = esp_http_client_read(client, body+total, sizeof(body)-1-total)) > 0){
        total+=n;

    }

    if (n<0){
        out_printf ("HTTPS read failed\n");
        err = ESP_FAIL;
        goto cleanup;
    }

    body[total] = '\0'; 

    cJSON *resp = cJSON_Parse(body);

    if (resp == NULL) {
        out_printf("Bad reply\n");
        err = ESP_FAIL;
        goto cleanup;
    } else {

        cJSON *lines = cJSON_GetObjectItem(resp, "lines");
        cJSON *line;
        cJSON_ArrayForEach(line, lines) {
            if (cJSON_IsString(line)) out_printf("%s\n", line->valuestring);
        }
        cJSON_Delete(resp);
    }
    
   cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    cJSON_free(json_string);
    cJSON_Delete(root);
    return err; 
}


esp_err_t app_relay_voice(const int16_t *audio, size_t length, relay_answer_t *answer)
{
    answer->count = 0;
    answer->question_lines = 0;
    for (int i = 0; i < ANSWER_MAX_LINES; i++) {
        answer->starts_paragraph[i] = false;
    }

    esp_http_client_config_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,   // check the server is really ours
        .url = RELAY_VOICE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 310000,                         // a deep search can take 5 minutes
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    // Raw audio has no header of its own, so this says what the bytes are.
    esp_http_client_set_header(client, "Content-Type", "audio/L16; rate=16000");
    esp_http_client_set_header(client, "X-Device-Token", RELAY_TOKEN);

    esp_err_t err = ESP_FAIL;

    if (esp_http_client_open(client, length) != ESP_OK) {
        printf("VOICE_FAIL: can't connect\n");
        goto cleanup;
    }

    // One write might not send everything, so keep going until it's all sent.
    const char *data = (const char *)audio;
    size_t sent = 0;
    while (sent < length) {
        int n = esp_http_client_write(client, data + sent, length - sent);
        if (n <= 0) {
            printf("VOICE_FAIL: upload stopped at %u bytes\n", (unsigned)sent);
            goto cleanup;
        }
        sent = sent + n;
    }
    printf("VOICE_SENT: %u bytes\n", (unsigned)sent);

    // The upload is finished, so now we're waiting on Whisper and Claude.
    app_display_status("Thinking", "This can take a moment", DISPLAY_SPINNER);
    printf("UI: THINKING\n");

    // Now wait for the reply. Claude may take a while.
    if (esp_http_client_fetch_headers(client) < 0) {
        printf("VOICE_FAIL: no reply\n");
        goto cleanup;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        printf("VOICE_FAIL: relay error %d\n", status);
        goto cleanup;
    }

    // Read the whole reply into one buffer. It's static so it isn't on the stack.
    static char body[4096];
    int total = 0;
    int n;
    while ((n = esp_http_client_read(client, body + total, sizeof(body) - 1 - total)) > 0) {
        total = total + n;
    }
    if (n < 0) {
        printf("VOICE_FAIL: read failed\n");
        goto cleanup;
    }
    body[total] = '\0';    // C strings must end with this

    // Turn the text into JSON data.
    cJSON *reply = cJSON_Parse(body);
    if (reply == NULL) {
        printf("VOICE_FAIL: bad reply\n");
        goto cleanup;
    }

    // Copy each line into our box. We must copy them, because
    // cJSON_Delete below frees the memory they live in.
    cJSON *lines = cJSON_GetObjectItem(reply, "lines");
    cJSON *line;
    cJSON_ArrayForEach(line, lines) {
        if (cJSON_IsString(line) && answer->count < ANSWER_MAX_LINES) {
            strlcpy(answer->lines[answer->count], line->valuestring, ANSWER_LINE_SIZE);
            answer->count = answer->count + 1;
        }
    }
    // How many of those lines are the question, so the screen can color them.
    cJSON *question_lines = cJSON_GetObjectItem(reply, "question_lines");
    if (cJSON_IsNumber(question_lines)) {
        answer->question_lines = question_lines->valueint;
    }
    // Which lines start a paragraph, so the screen can put a dot next to them.
    cJSON *paragraph_lines = cJSON_GetObjectItem(reply, "paragraph_lines");
    cJSON *paragraph_line;
    cJSON_ArrayForEach(paragraph_line, paragraph_lines) {
        if (cJSON_IsNumber(paragraph_line)) {
            int number = paragraph_line->valueint;
            if (number >= 0 && number < ANSWER_MAX_LINES) {
                answer->starts_paragraph[number] = true;
            }
        }
    }
    cJSON_Delete(reply);
    err = ESP_OK;

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}