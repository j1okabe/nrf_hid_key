#ifndef INIFILE_H
#define INIFILE_H
#include <Arduino.h>
#include <string.h>

char *inifileString(FatFile fp, char *header, char *detail, char *defaultData = NULL)
{
    unsigned int position, length, index = 0;
    char str[512], *output = NULL;
    bool hit, headerHit = false;
    if (!fp)
    {
        return defaultData;
    }
    fp.rewind();
#ifdef DEBUG
    Serial.print(header);
    Serial.print(":");
    Serial.print(detail);
    Serial.print(":");
#endif
    while (fp.available())
    {
        str[index] = fp.read();
        if (!(str[index] == 0x0d || str[index] == 0x0a))
        {
            if (index != 511)
                index++;
            continue;
        }
        str[index] = 0x00;
        if (str[0] == '[')
        {
            if (strchr(str, ']') != NULL)
            {
                headerHit = false;
                for (int i = 0;; i++)
                {
                    if (header[i] == 0)
                    {
                        if (str[1 + i] == ']')
                            headerHit = true;
                        break;
                    }
                    if (toupper(str[1 + i]) != toupper(header[i]))
                        break;
                }
            }
        }
        if (headerHit == true)
        {
            char *strpos = strchr(str, '=');
            if (strpos != NULL)
            {
                position = (strpos - str);
                if (position == strlen(detail))
                {
                    hit = true;
                    for (unsigned int i = 0; i < position; i++)
                    {
                        if (toupper(str[i]) != toupper(detail[i]))
                        {
                            hit = false;
                            break;
                        }
                    }
                    if (hit == true)
                    {
                        length = strlen(&str[position + 1]);
                        if (output != NULL)
                            free(output);
                        output = (char *)malloc(length + 1);
                        memcpy(output, &str[position + 1], length);
                        output[length] = 0;
                        break;
                    }
                }
            }
        }
        index = 0;
    }
    if (output == NULL)
    {
#ifdef DEBUG
        Serial.print("(none)");
#endif
        if (defaultData != NULL)
        {
#ifdef DEBUG
            Serial.println(defaultData);
#endif
            output = (char *)malloc(strlen(defaultData) + 1);
            strcpy(output, defaultData);
        }
        return defaultData;
    }
#ifdef DEBUG
    Serial.println(output);
#endif
    return output;
}

#endif