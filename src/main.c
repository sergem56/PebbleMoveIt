#include <pebble.h>
#include <math.h>

#define DEBUG false

#define BATCH_SIZE 10

// UI
static Window* mWindow = NULL;

static uint32_t sleepCounterPerPeriod = 0;
static uint32_t otherCounterPerPeriod = 0;

static uint32_t totalSteps = 0;
static int minuteCounter = 0;
static uint32_t stepsPerPeriod = 0;
static uint32_t oldSteps = 0;
static int segmentsInactive = 0;
static uint32_t visibleSegments = 0;
static uint32_t activeMinutes = 0;
static int dayNumber = 0;
static int lastMinute;

static int daysNo = 1;
static int daysYes = 1;
static bool dailyGoalBuzzed = false;

static bool isMoving = false;
static bool isSleeping = false;
static bool needBuzz = false;
static int buzzNo = 0;

static char buffer[8];
static char bufferActive[5];
static char bufferDate[8];
static char bufferGoal[8];

static uint32_t dailyGoal;

static uint32_t steps = 0;

static TextLayer *s_time_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_active_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_date_layer;
static TextLayer *s_goal_layer;
static GFont s_time_font;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

static BitmapLayer *s_gauge_layer;
static GBitmap *s_gauge0_bitmap;
static GBitmap *s_gauge1_bitmap;
static GBitmap *s_gauge2_bitmap;
static GBitmap *s_gauge3_bitmap;
static GBitmap *s_gauge4_bitmap;

static BitmapLayer *s_performance_layer;
static GBitmap *s_perf0_bitmap;
static GBitmap *s_perf1_bitmap;
static GBitmap *s_perf2_bitmap;

static void updateGauge(void) {
    uint32_t needSegments = ceil(segmentsInactive/15);
    if(needSegments > 4){
        needSegments = 4;
    }
    
    /*
    char tmpStr[32];
    snprintf(tmpStr, 32, "%5d -> %5d", (int) needSegments, (int) segmentsInactive);
	app_log(APP_LOG_LEVEL_INFO, "Segments ", 0, tmpStr, mWindow);
    */
    
    if(needSegments != visibleSegments){
        
        switch(needSegments){
            case 0:
                visibleSegments = 0;
                bitmap_layer_set_bitmap(s_gauge_layer, s_gauge0_bitmap);
                break;
            case 1:
                visibleSegments = 1;
                bitmap_layer_set_bitmap(s_gauge_layer, s_gauge1_bitmap);
                break;
            case 2:
                visibleSegments = 2;
                bitmap_layer_set_bitmap(s_gauge_layer, s_gauge2_bitmap);
                break;
            case 3:
                visibleSegments = 3;
                bitmap_layer_set_bitmap(s_gauge_layer, s_gauge3_bitmap);
                break;
            case 4:
                visibleSegments = 4;
                bitmap_layer_set_bitmap(s_gauge_layer, s_gauge4_bitmap);
                break;
        }
    }
}

static void updateSteps(void){
    snprintf(buffer, 7, "%.5d", (int) (totalSteps));
    text_layer_set_text(s_steps_layer, buffer);
    snprintf(bufferActive, 5, "-%3d", (int) segmentsInactive); //activeMinutes);
    text_layer_set_text(s_active_layer, bufferActive);
}

static void battery_handler(BatteryChargeState charge_state) {
  static char s_battery_buffer[5];

  if (charge_state.is_charging) {
    snprintf(s_battery_buffer, sizeof(s_battery_buffer), "+%d%%", charge_state.charge_percent);
  } else {
    snprintf(s_battery_buffer, sizeof(s_battery_buffer), "%d%%", charge_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, s_battery_buffer);
}

float my_sqrt(const float num) {
  const uint MAX_STEPS = 30;
  const float MAX_ERROR = 1.0;
  
  float answer = num;
  float ans_sqr = answer * answer;
  uint step = 0;
  while((ans_sqr - num > MAX_ERROR) && (step++ < MAX_STEPS)) {
      if(answer != 0){
          answer = (answer + (num / answer)) / 2;
      }
    ans_sqr = answer * answer;
  }
  return answer;
}

static void buzzAchieved(void){
    
    uint32_t segments[] = {300, 150, 150, 120, 150, 300, 400};
 
    //Create a VibePattern structure with the segments and length of the pattern as fields
    VibePattern pattern = {
        .durations = segments,
        .num_segments = 7,//ARRAY_LENGTH(segments),
    };
    //Trigger the custom pattern to be executed
    vibes_enqueue_custom_pattern(pattern);
}

static float lastEv = 0;
static int lastStepNo;
static uint32_t stepsInARow = 0;

static int totalEv = 0;
static char tmpStr[14];

// This one is better in false detection - almost no "sitting" steps and more accurate in walking steps counting
// Steady pace walking - accuracy 100% - tested on 200-step blocks.
// Sitting - almost no false steps.
// Driving - +30-70 steps per 30-min drive - I think it's acceptable.
// Misfit app counts about 30% more steps, however it's known for counting extra steps. It is very hard to avoid this - 
// you count steps by detecting your hand moves...
void processAccelerometerData(AccelData* acceleration, uint32_t size) 
{
    float evMax = 0;
    float evMin = 5000000;
    float evMean = 0;
    float ev[10];
    float evAv[10];
    
    for(uint32_t i=0;i<size&&i<10;i++){
        ev[i] = my_sqrt(acceleration[i].x*acceleration[i].x + acceleration[i].y*acceleration[i].y + acceleration[i].z*acceleration[i].z);
    }
    // Need to eliminate slow change and detect peaks...
    // 1. Find very simple moving average
    
    evAv[0] = (lastEv + ev[0])/2;
    evAv[1] = (lastEv + ev[0]+ev[1])/3;
    for(int i=2;i<10;i++){
        evAv[i] = (ev[i]+ev[i-1]+ev[i-2])/3;
    }
    lastEv = ev[9];
    
    /*
    // This one is OK, but requires more CPU
    evAv[0] = (lastEv + ev[0])/2;
    for(int i=1;i<10;i++){
        evAv[i] = 0.18*ev[i]+0.82*evAv[i-1];
    }
    lastEv = ev[9];
    */
    
    // 2. Find peaks above average line and higher than minimum energy
    for(int i=0;i<10;i++){
        // 3 steps per second = 180 steps per minute maximum, who can run faster? 
        // Well, tests show that it drops steps somehow... Back to 1. 0.8 and 24.000 are pure empirical values.
        // Values greater than 24.000 give less false detections, but can skip steps if you, for example, have something
        // in your hand and don't move it much.
        // One strange thing - movements in horizontal plane even with considerable amplitude, give very small ev value.
        // So, walking in an elliptical trainer with moving handles counts only about 10% of real steps... Donna what to do 
        // with that. Perhaps lowering evMeanMax would solve the problem, but will certainly give more false steps in other
        // conditions.
        evMean += ev[i];
        if(lastStepNo > 2 && ev[i] > evAv[i]*1.05 && evAv[i] > 70){ // evMean*1.1 && evMean > 20000){
            steps++;
            //snprintf(tmpStr, 31, "%d+%d", (int)ev[i],(int)evAv[i]);
            if(lastStepNo < 9)stepsInARow++; // if last step was detected less than 0.9 seconds before current, count it as a sequence
            else{
                stepsInARow = 0;
                steps = 0;
            }
            lastStepNo = 0;
        }else{
            //snprintf(tmpStr, 31, "%d;%d", (int)ev[i],(int)evAv[i]);
        }
        
        //static char tmpStr[32];
        //app_log(APP_LOG_LEVEL_INFO, "Extr ", 0, tmpStr, mWindow);
        
        lastStepNo++;
    }
        /*
        snprintf(tmpStr, 63, "%d/%d, %d/%d", (int)ev[0],(int)evAv[0],(int)ev[1],(int)evAv[1]);
        app_log(APP_LOG_LEVEL_INFO, "Extr ", 0, tmpStr, mWindow);
    */
    // 6. Count steps only if there are several steps in a row to avoid random movements. Downside is that
    // if you step less than 7 steps in a row or stop for a while it would not count them.
    if(stepsInARow > 7){
        totalSteps += steps;
        if(totalSteps >= dailyGoal && !dailyGoalBuzzed){
            dailyGoalBuzzed = true;
            buzzAchieved();
        }
        if(steps > 0){
            updateSteps();
            updateGauge();
        }else{
            
        }
        steps = 0;
    }
    
    // 7. Detect off-the-wrist condition not to buzz when nobody hears it. Or at night.
    // Off the wrist: Mean < 150 or Max < ~200-400
    // Seating: Mean up to 24.000
    // Walking: mean ~24.000
    // Jogging: ?
    
    //snprintf(tmpStr, 63, "%d", (int)evMean);
    //app_log(APP_LOG_LEVEL_INFO, "Extr ", 0, tmpStr, mWindow);
    //if(evMax < 260){
    //if(evMean < 130){
    if(evMean < 10300){//10360
        sleepCounterPerPeriod++;
    }else{
        otherCounterPerPeriod++;
    }
    evMean = 0;
    
    //static char tmpStr2[32];
    //snprintf(tmpStr2, 32, "%5d:%5d", (int)evMeanMax, (int)evMax);
    //snprintf(tmpStr, 128, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", (int)ev[0],(int)ev[1],(int)ev[2],(int)ev[3],(int)ev[4],(int)ev[5],(int)ev[6],(int)ev[7]),(int)ev[8],(int)ev[9]);
	//app_log(APP_LOG_LEVEL_INFO, "Extr ", 0, tmpStr2, mWindow);
    //APP_LOG(APP_LOG_LEVEL_INFO, "%d:%d", (int)evMeanMax, (int)evMax);
    
}

// This one is pretty accurate, giving about 5-8% less steps, but counts some false steps that compensates it.
void processAccelerometerDataWorking(AccelData* acceleration, uint32_t size) 
{
    float evMax = 0;
    float evMin = 5000000;
    float evMean = 0;
    float ev[10];
    
    for(uint32_t i=0;i<size&&i<10;i++){
        ev[i] = my_sqrt(acceleration[i].x*acceleration[i].x + acceleration[i].y*acceleration[i].y + acceleration[i].z*acceleration[i].z);
        ev[i] *= ev[i]; // make peaks sharper
        if(ev[i] > evMax) evMax = ev[i];
        if(ev[i] < evMin) evMin = ev[i];
        evMean += ev[i];
    }
    evMean /= 10;//min(10,step);
    evMean -= evMin;
    evMax -= evMin;
    
    /*
    char tmpStr[64];
    snprintf(tmpStr,64, "%5d-%5d-%5d-%5d-%5d-%5d-%5d-%5d-%5d-%5d", (int) ev[0],(int) ev[1],(int) ev[2],(int) ev[3],(int) ev[4],(int) ev[5],(int) ev[6],(int) ev[7],(int) ev[8],(int) ev[9]);
    app_log(APP_LOG_LEVEL_INFO, "energy",0, tmpStr, mWindow);
    */
    
    // filter out too frequent peaks, anyway, only 3 steps per second seem sane
    // the filter is a bit rough, but who cares!
    for(int i=0;i<9;i++){
        if(ev[i+1] == 0)continue;
        float t = ev[i]/ev[i+1];
        if(t<1.2 && t>=1){
            ev[i+1] = evMin;
        }else if(t<1 && t>0.8){
            ev[i] = evMin;
        }
    }
    
    //int stepCounted = 0;
    for(int i=0;i<10;i++){
        //ev[i] -= evMin; // well, I should do it, but it works better without! Or I need to tweak the next line...
        if(ev[i] > evMean+(evMax-evMean)*0.5 && evMean > 575000){
            steps++;
        }
    }

    totalSteps += steps == 1?1:steps/2;
    if(steps > 0){
        //mCurrentType = 2;
        updateSteps();
        updateGauge();
    }else{
            
    }
    steps = 0;
    
    // Normalized squared:
    // Sleeping: Mean < 30.000
    // Seating: Mean
    // Walking: mean ~600.000
    // Jogging:
    if(evMean < 20000){
        sleepCounterPerPeriod++;
    }else{
        otherCounterPerPeriod++;
    }
    
    /*
    char tmpStr[64];
    snprintf(tmpStr, 64, "%d-%d-%d:%d,%d,%d,%d,%d,%d", (int) evMin/100, (int)evMean/100, (int)evMax/100, (int)ev[1]/100,(int)ev[2]/100,(int)ev[3]/100,(int)ev[4]/100,(int)ev[5]/100,(int)ev[6]/100);
	app_log(APP_LOG_LEVEL_INFO, "Extr ", 0, tmpStr, mWindow);
    */
}

// Vibrate more insistive each 15 minutes
static void buzz(void){
    //char msg[] = "buzz called";
	//app_log(APP_LOG_LEVEL_DEBUG, "DEBUG", 0, msg, mWindow);
    
    //Create an array of ON-OFF-ON etc durations in milliseconds
    uint32_t* segmentsPtr;
    int len = 3;
    uint32_t segments[]  = {50, 300, 50+buzzNo};
    segmentsPtr = segments;
    /*
    uint32_t segments[]  = {300, 300, 300, 1000, 300, 300, 190};
    uint32_t segments1[] = {400, 300, 400, 2000, 300, 300, 300, 3000, 190};
    uint32_t segments2[] = {500, 300, 500, 2000, 300, 300, 300, 4000, 300, 4000, 190};
    uint32_t segments3[] = {600, 300, 600, 2000, 300, 300, 300, 5000, 300, 6000, 190};
    switch(buzzNo){
        case 0:{
            segmentsPtr = segments;
            len = 7;
            break;
        }  
        case 1:{
            segmentsPtr = segments1;
            len = 9;
            break;
        }
        case 2:{
            segmentsPtr = segments2;
            len = 11;
            break;
        }
        default:{
            segmentsPtr = segments3;
            len = 11;
            break;
        }
            
    }
*/ 
    //Create a VibePattern structure with the segments and length of the pattern as fields
    VibePattern pattern = {
        .durations = segmentsPtr,
        .num_segments = len,//ARRAY_LENGTH(segmentsPtr),
    };
    //Trigger the custom pattern to be executed
    vibes_enqueue_custom_pattern(pattern);
    
    //buzzNo++;
    buzzNo += 50;
    if(buzzNo > 500)buzzNo = 500;
}

static void updateGoal(void){
    snprintf(bufferGoal, 8, "%2d.%.2dK", (int)(dailyGoal/1000),(int)(dailyGoal%1000)/10);
    text_layer_set_text(s_goal_layer, bufferGoal);
    
    int daysT = daysYes+daysNo;
    if(daysT == 0){
        daysT = 1;
    }
    float prcnt = daysYes*100/daysT;
    if(prcnt < 30){
        bitmap_layer_set_bitmap(s_performance_layer, s_perf2_bitmap);
    }else if(prcnt > 50){
        bitmap_layer_set_bitmap(s_performance_layer, s_perf0_bitmap);
    }else{
        bitmap_layer_set_bitmap(s_performance_layer, s_perf1_bitmap);
    }
}

static void update_time(void) {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

    //APP_LOG(APP_LOG_LEVEL_INFO, "%d", totalEv/10);
    
    
  // Create a long-lived buffer
  static char buffer[] = "00:00";

  // Write the current hours and minutes into the buffer
  if(clock_is_24h_style() == true) {
    // Use 24 hour format
    strftime(buffer, sizeof("00:00"), "%H:%M", tick_time);
  } else {
    // Use 12 hour format
    strftime(buffer, sizeof("00:00"), "%I:%M", tick_time);
  }

    /*
    char tmpStr[32];
    snprintf(tmpStr, 32, "%5d (+) -> %5d", (int) sleepCounterPerPeriod, (int) otherCounterPerPeriod);
	app_log(APP_LOG_LEVEL_INFO, "State", 0, tmpStr, mWindow);
    */
    
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, buffer);
    
    strftime(bufferDate, 7, "%a %d", tick_time);
    text_layer_set_text(s_date_layer, bufferDate);
    
    if(tick_time->tm_min == lastMinute){
        return;
    }
    lastMinute = tick_time->tm_min;
    
    //isSleeping = (totalEv/10 < 1600); //sleepCounterPerPeriod > otherCounterPerPeriod*1.4; // or make it = otherCounterPerPeriod > 20?
    //totalEv = 0;
    isSleeping = (sleepCounterPerPeriod > 56);
    
    minuteCounter++;
    stepsPerPeriod = totalSteps - oldSteps;
    oldSteps = totalSteps;
    if(stepsPerPeriod > 50){ // count active if you made more than 40 steps per minute
        segmentsInactive -= stepsPerPeriod/4;// 30;
        activeMinutes++;
        isMoving = true;
        if(segmentsInactive <= 0){
            segmentsInactive = 0;
        }
        needBuzz = false;
        buzzNo = 0;
    //}else if(stepsPerPeriod < 2){// what if it is night?
    //    isMoving = false;
    }else if(stepsPerPeriod < 30 && !isSleeping){ // less than 20 steps - you're inactive
        segmentsInactive++;
        //segmentsInactive += 14; // TEST
        isMoving = false;
    }else{
        isMoving = false;
    }
    stepsPerPeriod = 0;
    sleepCounterPerPeriod = 0;
    otherCounterPerPeriod = 0;
    
    if(segmentsInactive > 120){
        segmentsInactive = 120; // to reset the timer you need 360 steps max
    }
    
    if(!needBuzz && segmentsInactive > 59 && !isMoving){
        // buzz
        needBuzz = true;
        minuteCounter = 0;
    }
    
    if(!isSleeping && needBuzz && minuteCounter % 6 == 0){
        buzz();
        minuteCounter = 0;
    }
    
    if(dayNumber != tick_time->tm_yday){
        // Next day, reset all
        if(totalSteps < dailyGoal){ // reduce your next daily goal by 5%
            dailyGoal *= 0.95;
            daysNo++;
            persist_write_int(1, daysNo);
        }else{
            dailyGoal *= 1.05;
            daysYes++;
            persist_write_int(2, daysYes);
        }
        dailyGoal = ceil(dailyGoal/10)*10;
        updateGoal();
        
        dayNumber = tick_time->tm_yday;
        totalSteps = 0;
        oldSteps = 0;
        activeMinutes = 0;
        dailyGoalBuzzed = false;
    }
    
    updateSteps();
    updateGauge();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void windowLoad(Window *window) {
	// Create & Setup UIs
	//Layer* windowLayer = window_get_root_layer(mWindow);
	//GRect bounds = layer_get_bounds(windowLayer);

	// ... //
    
    // Create GBitmap, then set to created BitmapLayer
    s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BG02);
    s_background_layer = bitmap_layer_create(GRect(0, 0, 144, 168));
    bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
    layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(s_background_layer));
    
    s_gauge_layer = bitmap_layer_create(GRect(6, 29, 97, 8));
    s_gauge0_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LINE0);
    s_gauge1_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LINE1);
    s_gauge2_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LINE2);
    s_gauge3_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LINE3);
    s_gauge4_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LINE4);
    bitmap_layer_set_bitmap(s_gauge_layer, s_gauge0_bitmap);
    layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(s_gauge_layer));
    
    // Performance
    s_performance_layer = bitmap_layer_create(GRect(9, 60, 20, 20));
    s_perf0_bitmap = gbitmap_create_with_resource(RESOURCE_ID_SMILE_FUN);
    s_perf1_bitmap = gbitmap_create_with_resource(RESOURCE_ID_SMILE_NEU);
    s_perf2_bitmap = gbitmap_create_with_resource(RESOURCE_ID_SMILE_SAD);
    layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(s_performance_layer));
    
    // Create time TextLayer
  s_steps_layer = text_layer_create(GRect(0, 34, 104, 30));
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_text_color(s_steps_layer, GColorBlack);
  text_layer_set_text(s_steps_layer, "00000");
    text_layer_set_font(s_steps_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));// FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentRight);
    // Add it as a child layer to the Window's root layer
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_steps_layer));
    
    s_active_layer = text_layer_create(GRect(107, 36, 33, 30));
    text_layer_set_background_color(s_active_layer, GColorClear);
    text_layer_set_text_color(s_active_layer, GColorBlack);
    text_layer_set_font(s_active_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_active_layer, GTextAlignmentLeft);
    text_layer_set_text(s_active_layer, ":000");
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_active_layer));
    
    // Goal
    s_goal_layer = text_layer_create(GRect(5, 36, 44, 30));
    text_layer_set_background_color(s_goal_layer, GColorClear);
    text_layer_set_text_color(s_goal_layer, GColorBlack);
    text_layer_set_font(s_goal_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_goal_layer, GTextAlignmentLeft);
    text_layer_set_text(s_goal_layer, "10.00K");
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_goal_layer));
    
    // Battery
    s_battery_layer = text_layer_create(GRect(104, 25, 34, 18));
    text_layer_set_background_color(s_battery_layer, GColorClear);
    text_layer_set_text_color(s_battery_layer, GColorBlack);
    text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
    BatteryChargeState charge_state = battery_state_service_peek();
    battery_handler(charge_state);
    //text_layer_set_text(s_battery_layer, "100%");
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_battery_layer));
    
    // Date
    s_date_layer = text_layer_create(GRect(62, 66, 72, 24));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorBlack);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentRight);
    //text_layer_set_text(s_date_layer, "THU 25");
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_date_layer));
    
  // Create time TextLayer
  //s_time_layer = text_layer_create(GRect(5, 99, 132, 38)); // for Pixel_LCD_7
    s_time_layer = text_layer_create(GRect(7, 75, 132, 60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_text(s_time_layer, "00:00");

    // Create GFont
    //s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LCD_BOLD_38)); // for Pixel_LCD_7
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LCD_BOLD_60));

    // Apply to TextLayer
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentRight);
    
  // Add it as a child layer to the Window's root layer
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_time_layer));
    
    //snprintf(buffer, 6, "%.5d", (int) (mCounter.steps));
    //text_layer_set_text(s_steps_layer, buffer);
    updateSteps();
    update_time();
    updateGauge();
    updateGoal();
}

static void windowUnload(Window *window) {
	if (DEBUG) {
		char msg[] = "windowUnload() called";
		app_log(APP_LOG_LEVEL_DEBUG, "DEBUG", 0, msg, mWindow);
	}

	// Destroy UIs
	// ... //
    // Destroy GBitmap
    gbitmap_destroy(s_background_bitmap);
    gbitmap_destroy(s_gauge0_bitmap);
    gbitmap_destroy(s_gauge1_bitmap);
    gbitmap_destroy(s_gauge2_bitmap);
    gbitmap_destroy(s_gauge3_bitmap);
    gbitmap_destroy(s_gauge4_bitmap);

    gbitmap_destroy(s_perf0_bitmap);
    gbitmap_destroy(s_perf1_bitmap);
    gbitmap_destroy(s_perf2_bitmap);
    
    // Destroy BitmapLayer
    bitmap_layer_destroy(s_background_layer);
    bitmap_layer_destroy(s_gauge_layer);
    bitmap_layer_destroy(s_performance_layer);
    
  // Destroy TextLayer
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_steps_layer);
    text_layer_destroy(s_active_layer);
    text_layer_destroy(s_battery_layer);
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_goal_layer);
    // Unload GFont
    fonts_unload_custom_font(s_time_font);
}

static void init(void) {
	// Load persistent values
    daysNo = persist_exists(1) ? persist_read_int(1) : 1;
    daysYes = persist_exists(2) ? persist_read_int(2) : 1;
    
	totalSteps = persist_exists(5) ? persist_read_int(5) : 0;
    segmentsInactive = persist_exists(6) ? persist_read_int(6) : 0;
    if(segmentsInactive > 90){
        segmentsInactive = 90;
    }
    
    //segmentsInactive = 0;
    
    activeMinutes = persist_exists(7) ? persist_read_int(7) : 0;
    oldSteps = persist_exists(8) ? persist_read_int(8) : 0;
    dayNumber = persist_exists(9) ? persist_read_int(9) : 0;
    dailyGoal = persist_exists(10) ? persist_read_int(10) : 8250;
    
    //dailyGoal = 8250; // TEST
    //mCounter.steps = 0;
    
	// For main window
	mWindow = window_create();

	window_set_window_handlers(
		mWindow,
		(WindowHandlers) {
			.load = windowLoad,
			.unload = windowUnload,
		}
	);
	window_stack_push(mWindow, true);

	// Setup accelerometer API
	accel_data_service_subscribe(BATCH_SIZE, &processAccelerometerData);
	accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
}

static void deinit(void) {
	if (DEBUG) {
		char msg[] = "deinit() called";
		app_log(APP_LOG_LEVEL_DEBUG, "DEBUG", 0, msg, mWindow);
	}

	// Save persistent values
	persist_write_int(5, totalSteps);
    persist_write_int(6, segmentsInactive);
    persist_write_int(7, activeMinutes);
    persist_write_int(8, oldSteps);
    persist_write_int(9, dayNumber);
    persist_write_int(10, dailyGoal);

	accel_data_service_unsubscribe();
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();

	window_destroy(mWindow);
}

int main(void) {
	init();

	// Main loop
	app_event_loop();

	deinit();
}
