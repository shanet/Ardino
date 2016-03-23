#include "audio.h"

#define TRIGGER_PIN 13
#define ECHO_PIN 12
#define LED_PIN 4
#define SPEAKER_PIN 11

#define NUM_ROARS 3
#define NUM_FLASHES 20

#define AUDIO_SAMPLE_RATE 8000
#define TRIGGER_DISTANCE 100 // cm
#define SPEED_OF_SOUND .034 // cm/microsecond

unsigned char const *sounddata_data = 0;
int sounddata_length = 0;
volatile uint16_t sample;
byte lastSample;

void setup() {
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  if(readDistance() < TRIGGER_DISTANCE) {
    for(int i=0; i<NUM_ROARS; i++) {
      rawr();
      flash();

      // Wait for the audio playback to finish before moving on
      delay(sizeof(samples) / AUDIO_SAMPLE_RATE * 1000);
    }
  }
}

void flash() {
  for(int i=0; i<NUM_FLASHES; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

void rawr() {
  startPlayback(samples, sizeof(samples));
}

float readDistance() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIGGER_PIN, LOW);

  // Divide by two to account for time to travel to/from the object
  return pulseIn(ECHO_PIN, HIGH) * SPEED_OF_SOUND / 2;
}

ISR(TIMER1_COMPA_vect) {
  // This function is called at SAMPLE_RATE to load the next sample
  if(sample >= sounddata_length) {
    if(sample == sounddata_length + lastSample) {
      stopPlayback();
    } else {
      // Ramp down to zero to reduce the click at the end of playback
      OCR2A = sounddata_length + lastSample - sample;
    }
  } else {
    OCR2A = pgm_read_byte(&sounddata_data[sample]);
  }

  sample++;
}

void startPlayback(unsigned char const *data, int length) {
  sounddata_data = data;
  sounddata_length = length;

  pinMode(SPEAKER_PIN, OUTPUT);

  // Set up Timer 2 to do pulse width modulation on the speaker pin

  // Use internal clock (datasheet p.160)
  ASSR &= ~(_BV(EXCLK) | _BV(AS2));

  // Set fast PWM mode  (p.157)
  TCCR2A |= _BV(WGM21) | _BV(WGM20);
  TCCR2B &= ~_BV(WGM22);

  // Do non-inverting PWM on pin OC2A (p.155)
  // On the Arduino this is pin 11
  TCCR2A = (TCCR2A | _BV(COM2A1)) & ~_BV(COM2A0);
  TCCR2A &= ~(_BV(COM2B1) | _BV(COM2B0));

  // No prescaler (p.158)
  TCCR2B = (TCCR2B & ~(_BV(CS12) | _BV(CS11))) | _BV(CS10);

  // Set initial pulse width to the first sample
  OCR2A = pgm_read_byte(&sounddata_data[0]);

  // Set up Timer 1 to send a sample every interrupt
  cli();

  // Set CTC mode (Clear Timer on Compare Match) (p.133)
  // Have to set OCR1A *after*, otherwise it gets reset to 0!
  TCCR1B = (TCCR1B & ~_BV(WGM13)) | _BV(WGM12);
  TCCR1A = TCCR1A & ~(_BV(WGM11) | _BV(WGM10));

  // No prescaler (p.134)
  TCCR1B = (TCCR1B & ~(_BV(CS12) | _BV(CS11))) | _BV(CS10);

  // Set the compare register, OCR1A
  // OCR1A is a 16-bit register, so we have to do this with interrupts disabled to be safe
  OCR1A = F_CPU / AUDIO_SAMPLE_RATE;

  // Enable interrupt when TCNT1 == OCR1A (p.136)
  TIMSK1 |= _BV(OCIE1A);

  lastSample = pgm_read_byte(&sounddata_data[sounddata_length-1]);
  sample = 0;

  sei();
}

void stopPlayback() {
  // Disable playback per-sample interrupt
  TIMSK1 &= ~_BV(OCIE1A);

  // Disable the per-sample timer completely
  TCCR1B &= ~_BV(CS10);

  // Disable the PWM timer
  TCCR2B &= ~_BV(CS10);

  digitalWrite(SPEAKER_PIN, LOW);
}
