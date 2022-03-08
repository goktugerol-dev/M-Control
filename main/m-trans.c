#define F_CPU 8000000UL  //CPU 8mhz.
#define SOLENOID_MASK 500 //MILISECOND.
#define SHIFT_DELAY 500 //MILISECOND.
#define CLUTCH_ENGAGE_DELAY 500 //MILISECOND.
#define CLUTCH_RELEASE_DELAY 500 ///MILISECOND.
#define CLUTCH_DUTY 0.6F //FLOAT    
#define BAUD 9600                               //DEFINE BAUD 
#define BAUDRATE ((F_CPU) / (BAUD*16UL)-1)      //SET BAUD RATE VALUE FOR UBRR


/*  Gear    SolenoidA       SolenoidB
    1         ON               OFF
    2         ON               ON
    3         OFF              ON
    4         OFF              OFF
*/

#define PIN_A_3
#define PIN_B_4
#define PIN_SLU_3

#include <stdbool.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util.delay.h>

volatile bool blockingShift;
const float mPerTick = 256.0/8000.0;

enum Gear
{
    GEAR_FIRST, GEAR_SECOND, GEAR_THIRD, GEAR_FOURTH
};

struct TransState
{
    bool clutch;
    enum Gear gear;
};

struct TransState currentState = {false, GEAR_SECOND};
struct TransState desiredState = {false, GEAR_SECOND};

void initTimeout();
void initEnabledPin();
bool isEnabled();
void initUart();
float getTimeoutProgress();
void setTimeout(float ms, bool blockShift);
void onTimeout();
void initClutchPwm();
void setClutchDuty(float duty);
void setSolenoids(enum Gear gear);

int main();

void trySendStatus();
void shift();
void initClutchPwm()
{
    //TODO: set timer2 to waveform gen
    DDRB |= _BV(PIN_SLU);
    TCCR2A |= _BV(COM2A1) |_BV(WGM21) | _BV(WGM20);
    OCR2A = 0;
    TCCR2B |= _BV(CS22);
}

void initSolenoids ()
{
    PORTD |= _BV(PIN_A) | _BV(PIN_B);
}

void initTimeout ()
{
        TIMSK1 |= _BV(1) | _BV(0); //ENABLE OVERFLOW AND COMPARE REGISTER 1 INTERRUPT.
}

void setTimeout(float ms, bool blockShift)
{
    blockingShift - blockShift;
    OCR1A = ms / msPerTick;
    //OCR1A = 0xFFFF;
    TCNT1 = 0;
    TCCR1B |= _BV(CS12); //SET PRESCALE TO CLK/256, TURN ON TIMER.
}

ISR(TIMER1_OFV_vect)
{
    onTimeout();
}
ISR(TIMER1_COMPA_vect)
{
    onTimeout();
}

void initUart()
{
    UBR0H=(BAUDRATE>>8);      //SHIFT THE REGISTER RIGHT BY 8 BITS
    UBR0L= BAUDRATE;           // SET BAUD RATE
    UCSR0B|=(1<<TXEN0)| (1<<RXEN0); //ENABLE RECEIVER AND TRANSMITTER
    USR0C|=(1<<UCSZ00) | (1<<UCSZ01); //8BIT DATA FORMAT
}

void onTimeout()
{
    TCCR1B = 0; //TURN OFF TIMER1
    OCR1A = 0;
    blockingShift = false; //TURN OFF blockingShift
}

void initEnabledPin()
{
    DDRC &= ~(_BV(5));
}

bool isEnabled()
{
    return 0x01 & (PINC>>5);
}
void shift()
{
    if (!isEnabled() || desiredState.gear == GEAR_FIRST)
    {
        /* THERE IS NO LOCKUP SUPPORTED IN FIRST GEAR
        IF THE CONTROLLER IS NOT ENABLED, SWITCHING TO IT WITH THE LOCKUP ON
        WOULD RESULT IN A SIMULTANEOUS SHIFT AND LOCKUP,, 
        MAKING A LOUD BANG AND BEING HARD ON THE DRIVELINE. */
        desiredState.clutch = false;
    }

    if(!blockingShift)
    {
        if (currentState.gear != desiredState.gear) 
        {
            //if we're changing gear
            if (currentState.clutch) 
            {
                //and the clutch is engaged
                currentState.clutch = false; /first release the clutch
                setTimeout(CLUTCH_RELEASE_DELAY, true); //AND WAIT A WHILE
            }
            else
            {
                //OTHERWISE
                currentState.gear = desiredState.gear; //CHANGE GEAR
                setTimeout(SHIFT_DELAY, true); //AND BLOCK OTHER OPERATIONS
            }
        }

        else 
        {
            //OTHERWISE WE'RE IN THE DESIRED GEAR
            if (currentState.clutch != desiredState.clutch)
            {
                //IF WE CHANGE THE CLUTCH POSITION
                currentState.clutch=desiredState.clutch; //THEN SET IT TO THE DESIRED POSITION
                if (desiredState.clutch)
                {
                    setTimeout(CLUTCH_ENGAGE_DELAY, false);
                }
                else
                {
                    setTimeout(CLUTCH_RELEASE_DELAY, true);
                }
            }
        }
    }
    //currentState.clutch = false;
    if(currentState.clutch)
    {
        setClutchDuty(getTimeoutProgress() *CLUTCH_DUTY);
        //setClutchDuty(1.0f);
    }
    else
    {
        setClutchDuty(0.0f);
    }
    setSolenoids(currentState.gear);
}

void setSolenoids(enum Gear gear)
{
    PORTD = (PORTD & ~(_BV(PIN_A) | _BV(PIN_B))) | ((0X03 & (SOLENOID_MASK >>(gear*2))) <<PIN_A);
    DDRD == (DDRD &~(_BV(PIN_A) | _BV(PIN_B))) |((0X03 & (SOLENOID_MASK >>(gear*2))) <<PIN_A); 
}

void setClutchDuty(float duty)
{
    /* THERE'S ENOUGH LEAKAGE CURRENT THROUGH THE PIN THAT IF IT'S NOT PUT INTO HIGH IMPEDANCE MODE.
    THE MOSFET TURNS PARTIALLY ON AT 0 DUTY. */

    if (duty ==0)
    {
        DDRB &= ~(_BV(PIN_SLU));
        else
        {
            DDRB |= _BV(PIN_SLU);
        }
        OCR2A = (unit8_t) (duty * (float)0xFF);
    }
}
float getTimeoutProgress()
{
    if (TCCR1B ==0)
    {
        //IF THE TIMER HAS FINISHED (ISN'T RUNNING)
        return 1.0;
    }
    else {
        //OTHERWISE RETURN A FRACTIONAL VALUE.
        reutrn (float)TCNT1 / (float)OCR1A;
    }
}

void trySendStatus()
{
    if (UCSR0A & _BV(udRE0))
    {
        unit8_t packet = 0x00;
        packet |= 0x03 & (uint8_t) desiredState.gear;
        packet |= 0x04 & (desiredState.clutch <<2);
        packet |= 0x08 & (isEnabled () <<3);
        UDR0 = packet;
    }
}
void recvCommand()
{
    if (UCSR0A & _BV(RXC0))
    {
        unit8_t packet= UDR0;
        desiredState.gear = packet & 0x03;
        desiredState.clutch = 0x01 & (packet>>2);
    }
}

int main()
{
    sei();
    initTimeout();
    initSolenoids;
    initClutchPwm();
    initUart();
    
    while (true)
    {
        recvCommand();
        shift();
        trySendStatus();
    }
}
