
#include "drawingFunctions.h"
#include "LilyGo-EPD-4-7-OWM-Weather-Display.h"


//Icon modifiers: scale, offset, linesize
const IconSize SmallIcon(8, 10, 5); // Previously sizes were defined as Small - scale 8
const IconSize MediumIcon(12, 25, 5);
const IconSize ForecastIcon(18, 32, 5);
const IconSize LargeIcon(20, 35, 5); // Large 20

int wifi_signal, CurrentHour = 0, CurrentMin = 0, CurrentSec = 0, EventCnt = 0, vref = 1100;

GFXfont currentFont;
uint8_t *framebuffer;

void DrawBattery(int x, int y, float voltage, uint8_t percentage)
{           
        drawRect(x + 25, y - 14, 40, 15, Black);
        fillRect(x + 65, y - 10, 4, 7, Black);
        fillRect(x + 27, y - 12, 36 * percentage / 100.0, 11, Black);
        drawString(x + 75, y - 14, String(percentage) + "%  " + String(voltage, 1) + "v", LEFT);
}

void DrawRSSI(int x, int y, int rssi)
{
    int WIFIsignal = 0;
    int xpos = 1;
    for (int _rssi = -100; _rssi <= rssi; _rssi = _rssi + 20)
    {
        if (_rssi <= -20)
            WIFIsignal = 30; //            <-20dbm displays 5-bars
        if (_rssi <= -40)
            WIFIsignal = 24; //  -40dbm to  -21dbm displays 4-bars
        if (_rssi <= -60)
            WIFIsignal = 18; //  -60dbm to  -41dbm displays 3-bars
        if (_rssi <= -80)
            WIFIsignal = 12; //  -80dbm to  -61dbm displays 2-bars
        if (_rssi <= -100)
            WIFIsignal = 6; // -100dbm to  -81dbm displays 1-bar
        fillRect(x + xpos * 8, y - WIFIsignal, 6, WIFIsignal, Black);
        xpos++;
    }
}

// Symbols are drawn on a relative 10x10grid and 1 scale unit = 1 drawing unit
void addcloud(int x, int y, int scale, int linesize)
{
    fillCircle(x - scale * 3, y, scale, Black);                                                              // Left most circle
    fillCircle(x + scale * 3, y, scale, Black);                                                              // Right most circle
    fillCircle(x - scale, y - scale, scale * 1.4, Black);                                                    // left middle upper circle
    fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75, Black);                                       // Right middle upper circle
    fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, Black);                                 // Upper and lower lines
    fillCircle(x - scale * 3, y, scale - linesize, White);                                                   // Clear left most circle
    fillCircle(x + scale * 3, y, scale - linesize, White);                                                   // Clear right most circle
    fillCircle(x - scale, y - scale, scale * 1.4 - linesize, White);                                         // left middle upper circle
    fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75 - linesize, White);                            // Right middle upper circle
    fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, White); // Upper and lower lines
}

void addrain(int x, int y, int scale, const IconSize &size)
{
    if (&size == &SmallIcon)
    {
        setFont(OpenSans8B);
        drawString(x - 25, y + 12, "///////", LEFT);
    }
    else if(&size == &LargeIcon)
    {
        setFont(OpenSansB18);
        drawString(x - 60, y + 25, "///////", LEFT);
    }
}

void addsnow(int x, int y, int scale, const IconSize &size)
{
    if (&size == &SmallIcon)
    {
        setFont(OpenSans8B);
        drawString(x - 25, y + 15, "* * * *", LEFT);
    }
    else if(&size == &LargeIcon)
    {
        setFont(OpenSansB18);
        drawString(x - 60, y + 30, "* * * *", LEFT);
    }
}

void addtstorm(int x, int y, int scale)
{
    y = y + scale / 2;
    for (int i = 0; i < 5; i++)
    {
        drawLine(x - scale * 4 + scale * i * 1.5 + 0, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 0, y + scale, Black);
        if (scale != SmallIcon.scale)
        {
            drawLine(x - scale * 4 + scale * i * 1.5 + 1, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 1, y + scale, Black);
            drawLine(x - scale * 4 + scale * i * 1.5 + 2, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 2, y + scale, Black);
        }
        drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 0, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 0, Black);
        if (scale != SmallIcon.scale)
        {
            drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 1, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 1, Black);
            drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 2, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 2, Black);
        }
        drawLine(x - scale * 3.5 + scale * i * 1.4 + 0, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5, Black);
        if (scale != SmallIcon.scale)
        {
            drawLine(x - scale * 3.5 + scale * i * 1.4 + 1, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 1, y + scale * 1.5, Black);
            drawLine(x - scale * 3.5 + scale * i * 1.4 + 2, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 2, y + scale * 1.5, Black);
        }
    }
}

void addsun(int x, int y, int scale, const IconSize &size)
{
    int linesize = size.linesize;
    fillRect(x - scale * 2, y, scale * 4, linesize, Black);
    fillRect(x, y - scale * 2, linesize, scale * 4, Black);
    drawLine(x - scale * 1.3, y - scale * 1.3, x + scale * 1.3, y + scale * 1.3, Black);
    drawLine(x - scale * 1.3, y + scale * 1.3, x + scale * 1.3, y - scale * 1.3, Black);
    if (&size == &LargeIcon)
    {
        drawLine(1 + x - scale * 1.3, y - scale * 1.3, 1 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(2 + x - scale * 1.3, y - scale * 1.3, 2 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(3 + x - scale * 1.3, y - scale * 1.3, 3 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(1 + x - scale * 1.3, y + scale * 1.3, 1 + x + scale * 1.3, y - scale * 1.3, Black);
        drawLine(2 + x - scale * 1.3, y + scale * 1.3, 2 + x + scale * 1.3, y - scale * 1.3, Black);
        drawLine(3 + x - scale * 1.3, y + scale * 1.3, 3 + x + scale * 1.3, y - scale * 1.3, Black);
    }
    fillCircle(x, y, scale * 1.3, White);
    fillCircle(x, y, scale, Black);
    fillCircle(x, y, scale - linesize, White);
}

void addfog(int x, int y, int scale, int linesize, const IconSize &size)
{
    if (&size == &SmallIcon)
    {
        y -= 10;
        linesize = 1;
    }
    for (int i = 0; i < 6; i++)
    {
        fillRect(x - scale * 3, y + scale * 1.5, scale * 6, linesize, Black);
        fillRect(x - scale * 3, y + scale * 2.0, scale * 6, linesize, Black);
        fillRect(x - scale * 3, y + scale * 2.5, scale * 6, linesize, Black);
    }
}

void Sunny(int x, int y, const IconSize &size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;

    //if(&size == &SmallIcon){
        y = y - 12; // Shift small sun icon slightly up
    //}

    if (IconName.endsWith("n")){
        addmoon(x, y + Offset, scale, size);
    }
    scale = scale * 1.6;
    addsun(x, y, scale, size);
}

void MostlySunny(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n")){
        addmoon(x, y + Offset, scale, size);}
    addsun(x - scale * 1.8, y - scale * 1.8, scale, size);
    addcloud(x, y, scale, linesize);
}

void MostlyCloudy(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x, y, scale, linesize);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, size);
}

void Cloudy(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x + 15, y - 22, scale / 2, linesize); // Cloud top right
    addcloud(x - 10, y - 18, scale / 2, linesize); // Cloud top left
    addcloud(x, y, scale, linesize);               // Main cloud
}

void Rain(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, size);
}

void ExpectRain(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, size);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, size);
}

void ChanceRain(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, size);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, size);
}

void Tstorms(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x, y, scale, linesize);
    addtstorm(x, y, scale);
}

void Snow(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x, y, scale, linesize);
    addsnow(x, y, scale, size);
}

void Fog(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addcloud(x, y - 5, scale, linesize);
    addfog(x, y - 5, scale, linesize, size);
}

void Haze(int x, int y, IconSize size, String IconName)
{
    int scale = size.scale; 
    int Offset = size.Offset;
    int linesize = size.linesize;

    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, size);
    addsun(x, y - 5, scale * 1.4, size);
    addfog(x, y - 5, scale * 1.4, linesize, size);
}

void CloudCover(int x, int y, int CCover)
{
    addcloud(x - 9, y + 2 + 5, SmallIcon.scale * 0.3, 2); // Cloud top left
    addcloud(x + 3, y - 2 + 5, SmallIcon.scale * 0.3, 2); // Cloud top right
    addcloud(x, y + 10 + 5, SmallIcon.scale * 0.6, 2);    // Main cloud
    drawString(x + 25, y, String(CCover) + "%", LEFT);
}

void Visibility(int x, int y, String Visi)
{
    float start_angle = 0.52, end_angle = 2.61, Offset = 8;
    int r = 14;
    for (float i = start_angle; i < end_angle; i = i + 0.05)
    {
        drawPixel(x + r * cos(i), y - r / 2 + r * sin(i) + Offset, Black);
        drawPixel(x + r * cos(i), 1 + y - r / 2 + r * sin(i) + Offset, Black);
    }
    start_angle = 3.61;
    end_angle = 5.78;
    for (float i = start_angle; i < end_angle; i = i + 0.05)
    {
        drawPixel(x + r * cos(i), y + r / 2 + r * sin(i) + Offset, Black);
        drawPixel(x + r * cos(i), 1 + y + r / 2 + r * sin(i) + Offset, Black);
    }
    fillCircle(x, y + Offset, r / 4, Black);
    drawString(x + 20, y, Visi, LEFT);
}

void addmoon(int x, int y, int scale, const IconSize &size)
{
    if (&size == &LargeIcon)
    {
        fillCircle(x - 85, y - 100, uint16_t(scale * 0.8), Black);
        fillCircle(x - 57, y - 100, uint16_t(scale * 1.6), White);
    }
    else if (&size == &SmallIcon)
    {
        fillCircle(x - 28, y - 37, uint16_t(scale * 1.0), Black);
        fillCircle(x - 20, y - 37, uint16_t(scale * 1.6), White);
    }
}

void Nodata(int x, int y, IconSize &size, String IconName)
{
    if (&size == &LargeIcon)
        setFont(OpenSansB24);
    else if(&size == &SmallIcon){
        setFont(OpenSansB12);
    }
    drawString(x - 3, y - 10, "?", CENTER);
}

/* (C) D L BIRD
    This function will draw a graph on a ePaper/TFT/LCD display using data from an array containing data to be graphed.
    The variable 'max_readings' determines the maximum number of data elements for each array. Call it with the following parametric data:
    x_pos-the x axis top-left position of the graph
    y_pos-the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
    width-the width of the graph in pixels
    height-height of the graph in pixels
    Y1_Max-sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
    data_array1 is parsed by value, externally they can be called anything else, e.g. within the routine it is called data_array1, but externally could be temperature_readings
    auto_scale-a logical value (TRUE or FALSE) that switches the Y-axis autoscale On or Off
    barchart_on-a logical value (TRUE or FALSE) that switches the drawing mode between barhcart and line graph
    barchart_colour-a sets the title and graph plotting colour
    If called with Y!_Max value of 500 and the data never goes above 500, then autoscale will retain a 0-500 Y scale, if on, the scale increases/decreases to match the data.
    auto_scale_margin, e.g. if set to 1000 then autoscale increments the scale by 1000 steps.
*/
void DrawGraph(int x_pos, int y_pos, int gwidth, int gheight, float Y1Min, float Y1Max, String title, float DataArray[], int readings, boolean auto_scale, boolean barchart_mode)
{
#define auto_scale_margin 0 // Sets the autoscale increment, so axis steps up fter a change of e.g. 3
#define y_minor_axis 5      // 5 y-axis division markers
    setFont(OpenSansB10);
    int maxYscale = -10000;
    int minYscale = 10000;
    int last_x, last_y;
    float x2, y2;
    if (auto_scale == true)
    {
        for (int i = 1; i < readings; i++)
        {
            if (DataArray[i] >= maxYscale)
                maxYscale = DataArray[i];
            if (DataArray[i] <= minYscale)
                minYscale = DataArray[i];
        }
        maxYscale = round(maxYscale + auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Max
        Y1Max = round(maxYscale + 0.5);
        if (minYscale != 0)
            minYscale = round(minYscale - auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Min
        Y1Min = round(minYscale);
    }
    // Draw the graph
    last_x = x_pos + 1;
    last_y = y_pos + (Y1Max - constrain(DataArray[1], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;
    drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, Grey);
    drawString(x_pos - 20 + gwidth / 2, y_pos - 28, title, CENTER);
    for (int gx = 0; gx < readings; gx++)
    {
        x2 = x_pos + gx * gwidth / (readings - 1) - 1; // max_readings is the global variable that sets the maximum data that can be plotted
        y2 = y_pos + (Y1Max - constrain(DataArray[gx], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight + 1;
        if (barchart_mode)
        {
            fillRect(last_x + 2, y2, (gwidth / readings) - 1, y_pos + gheight - y2 + 2, Black);
        }
        else
        {
            drawLine(last_x, last_y - 1, x2, y2 - 1, Black); // Two lines for hi-res display
            drawLine(last_x, last_y, x2, y2, Black);
        }
        last_x = x2;
        last_y = y2;
    }
    //Draw the Y-axis scale
#define number_of_dashes 20
    for (int spacing = 0; spacing <= y_minor_axis; spacing++)
    {
        for (int j = 0; j < number_of_dashes; j++)
        { // Draw dashed graph grid lines
            if (spacing < y_minor_axis)
                drawFastHLine((x_pos + 3 + j * gwidth / number_of_dashes), y_pos + (gheight * spacing / y_minor_axis), gwidth / (2 * number_of_dashes), Grey);
        }
        if ((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing) < 5 || title == TXT_PRESSURE_IN)
        {
            drawString(x_pos - 10, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
        }
        else
        {
            if (Y1Min < 1 && Y1Max < 10)
            {
                drawString(x_pos - 3, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
            }
            else
            {
                drawString(x_pos - 7, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 0), RIGHT);
            }
        }
    }
    for (int i = 0; i < 3; i++)
    {
        drawString(20 + x_pos + gwidth / 3 * i, y_pos + gheight + 10, String(i) + "d", LEFT);
        if (i < 2)
            drawFastVLine(x_pos + gwidth / 3 * i + gwidth / 3, y_pos, gheight, LightGrey);
    }
}

void drawString(int x, int y, String text, alignment align)
{
    char *data = const_cast<char *>(text.c_str());
    int x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
    int w, h;
    int xx = x, yy = y;
    get_text_bounds(&currentFont, data, &xx, &yy, &x1, &y1, &w, &h, NULL);
    if (align == RIGHT)
        x = x - w;
    if (align == CENTER)
        x = x - w / 2;
    int cursor_y = y + h;
    write_string(&currentFont, data, &x, &cursor_y, framebuffer);
}

void fillCircle(int x, int y, int r, uint8_t color)
{
    epd_fill_circle(x, y, r, color, framebuffer);
}

void drawFastHLine(int16_t x0, int16_t y0, int length, uint16_t color)
{
    epd_draw_hline(x0, y0, length, color, framebuffer);
}

void drawFastVLine(int16_t x0, int16_t y0, int length, uint16_t color)
{
    epd_draw_vline(x0, y0, length, color, framebuffer);
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    epd_write_line(x0, y0, x1, y1, color, framebuffer);
}

void drawCircle(int x0, int y0, int r, uint8_t color)
{
    epd_draw_circle(x0, y0, r, color, framebuffer);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    epd_draw_rect(x, y, w, h, color, framebuffer);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    epd_fill_rect(x, y, w, h, color, framebuffer);
}

void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color)
{
    epd_fill_triangle(x0, y0, x1, y1, x2, y2, color, framebuffer);
}

void drawPixel(int x, int y, uint8_t color)
{
    epd_draw_pixel(x, y, color, framebuffer);
}

void setFont(GFXfont const &font)
{
    currentFont = font;
}

void arrow(int x, int y, int asize, float aangle, int pwidth, int plength)
{
    float dx = (asize - 10) * cos((aangle - 90) * PI / 180) + x; // calculate X position
    float dy = (asize - 10) * sin((aangle - 90) * PI / 180) + y; // calculate Y position
    float x1 = 0;
    float y1 = plength;
    float x2 = pwidth / 2;
    float y2 = pwidth / 2;
    float x3 = -pwidth / 2;
    float y3 = pwidth / 2;
    float angle = aangle * PI / 180 - 135;
    float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
    float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
    float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
    float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
    float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
    float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
    fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, Black);
}

void DrawSegment(int x, int y, int o1, int o2, int o3, int o4, int o11, int o12, int o13, int o14)
{
    drawLine(x + o1, y + o2, x + o3, y + o4, Black);
    drawLine(x + o11, y + o12, x + o13, y + o14, Black);
}

void DrawMoon(int x, int y, int dd, int mm, int yy, String hemisphere)
{
    const int diameter = 75;
    double Phase = NormalizedMoonPhase(dd, mm, yy);
    hemisphere.toLowerCase();
    if (hemisphere == "south")
        Phase = 1 - Phase;
    // Draw dark part of moon
    fillCircle(x + diameter - 1, y + diameter, diameter / 2 + 1, LightGrey);
    const int number_of_lines = 90;
    for (double Ypos = 0; Ypos <= number_of_lines / 2; Ypos++)
    {
        double Xpos = sqrt(number_of_lines / 2 * number_of_lines / 2 - Ypos * Ypos);
        // Determine the edges of the lighted part of the moon
        double Rpos = 2 * Xpos;
        double Xpos1, Xpos2;
        if (Phase < 0.5)
        {
            Xpos1 = -Xpos;
            Xpos2 = Rpos - 2 * Phase * Rpos - Xpos;
        }
        else
        {
            Xpos1 = Xpos;
            Xpos2 = Xpos - 2 * Phase * Rpos + Rpos;
        }
        // Draw light part of moon
        double pW1x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
        double pW1y = (number_of_lines - Ypos) / number_of_lines * diameter + y;
        double pW2x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
        double pW2y = (number_of_lines - Ypos) / number_of_lines * diameter + y;
        double pW3x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
        double pW3y = (Ypos + number_of_lines) / number_of_lines * diameter + y;
        double pW4x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
        double pW4y = (Ypos + number_of_lines) / number_of_lines * diameter + y;
        drawLine(pW1x, pW1y, pW2x, pW2y, White);
        drawLine(pW3x, pW3y, pW4x, pW4y, White);
    }
    drawCircle(x + diameter - 1, y + diameter, diameter / 2, Black);
}

