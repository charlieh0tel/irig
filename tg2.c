/*
 * tg.c generate WWV or IRIG signals for test
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <portaudio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0

#define VERSION (0)
#define ISSUE (23)
#define ISSUE_DATE "2007-02-12"

#define BUFLNG (400) /* buffer size */
#define WWV (0)      /* WWV encoder */
#define IRIG (1)     /* IRIG-B encoder */
#define OFF (0)      /* zero amplitude */
#define LOW (1)      /* low amplitude */
#define HIGH (2)     /* high amplitude */
#define DATA0 (200)  /* WWV/H 0 pulse */
#define DATA1 (500)  /* WWV/H 1 pulse */
#define PI (800)     /* WWV/H PI pulse */
#define M2 (2)       /* IRIG 0 pulse */
#define M5 (5)       /* IRIG 1 pulse */
#define M8 (8)       /* IRIG PI pulse */

#define N_ELEMENTS(X) (sizeof X / sizeof X[0])

#define NUL (0)

#define SECONDS_PER_MINUTE (60)
#define SECONDS_PER_HOUR (3600)

#define OUTPUT_DATA_STRING_LENGTH (200)

/*
 * Decoder operations at the end of each second are driven by a state
 * machine. The transition matrix consists of a dispatch table indexed
 * by second number. Each entry in the table contains a case switch
 * number and argument.
 */
struct progx {
  int sw;  /* case switch number */
  int arg; /* argument */
};

/*
 * Case switch numbers
 */
#define DATA (0)   /* send data (0, 1, PI) */
#define COEF (1)   /* send BCD bit */
#define DEC (2)    /* decrement to next digit and send PI */
#define MIN (3)    /* minute pulse */
#define LEAP (4)   /* leap warning */
#define DUT1 (5)   /* DUT1 bits */
#define DST1 (6)   /* DST1 bit */
#define DST2 (7)   /* DST2 bit */
#define DECZ (8)   /* decrement to next digit and send zero */
#define DECC (9)   /* decrement to next digit and send bit */
#define NODEC (10) /* no decerement to next digit, send PI */
#define DECX (11)  /* decrement to next digit, send PI, but no tick */
#define DATAX (12) /* send data (0, 1, PI), but no tick */

/*
 * WWV/H format (100-Hz, 9 digits, 1 m frame)
 */
struct progx progx[] = {
    {MIN, 800},    /* 0 minute sync pulse */
    {DATA, DATA0}, /* 1 */
    {DST2, 0},     /* 2 DST2 */
    {LEAP, 0},     /* 3 leap warning */
    {COEF, 1},     /* 4 1 year units */
    {COEF, 2},     /* 5 2 */
    {COEF, 4},     /* 6 4 */
    {COEF, 8},     /* 7 8 */
    {DEC, DATA0},  /* 8 */
    {DATA, PI},    /* 9 p1 */
    {COEF, 1},     /* 10 1 minute units */
    {COEF, 2},     /* 11 2 */
    {COEF, 4},     /* 12 4 */
    {COEF, 8},     /* 13 8 */
    {DEC, DATA0},  /* 14 */
    {COEF, 1},     /* 15 10 minute tens */
    {COEF, 2},     /* 16 20 */
    {COEF, 4},     /* 17 40 */
    {COEF, 8},     /* 18 80 (not used) */
    {DEC, PI},     /* 19 p2 */
    {COEF, 1},     /* 20 1 hour units */
    {COEF, 2},     /* 21 2 */
    {COEF, 4},     /* 22 4 */
    {COEF, 8},     /* 23 8 */
    {DEC, DATA0},  /* 24 */
    {COEF, 1},     /* 25 10 hour tens */
    {COEF, 2},     /* 26 20 */
    {COEF, 4},     /* 27 40 (not used) */
    {COEF, 8},     /* 28 80 (not used) */
    {DECX, PI},    /* 29 p3 */
    {COEF, 1},     /* 30 1 day units */
    {COEF, 2},     /* 31 2 */
    {COEF, 4},     /* 32 4 */
    {COEF, 8},     /* 33 8 */
    {DEC, DATA0},  /* 34 not used */
    {COEF, 1},     /* 35 10 day tens */
    {COEF, 2},     /* 36 20 */
    {COEF, 4},     /* 37 40 */
    {COEF, 8},     /* 38 80 */
    {DEC, PI},     /* 39 p4 */
    {COEF, 1},     /* 40 100 day hundreds */
    {COEF, 2},     /* 41 200 */
    {COEF, 4},     /* 42 400 (not used) */
    {COEF, 8},     /* 43 800 (not used) */
    {DEC, DATA0},  /* 44 */
    {DATA, DATA0}, /* 45 */
    {DATA, DATA0}, /* 46 */
    {DATA, DATA0}, /* 47 */
    {DATA, DATA0}, /* 48 */
    {DATA, PI},    /* 49 p5 */
    {DUT1, 8},     /* 50 DUT1 sign */
    {COEF, 1},     /* 51 10 year tens */
    {COEF, 2},     /* 52 20 */
    {COEF, 4},     /* 53 40 */
    {COEF, 8},     /* 54 80 */
    {DST1, 0},     /* 55 DST1 */
    {DUT1, 1},     /* 56 0.1 DUT1 fraction */
    {DUT1, 2},     /* 57 0.2 */
    {DUT1, 4},     /* 58 0.4 */
    {DATAX, PI},   /* 59 p6 */
    {DATA, DATA0}, /* 60 leap */
};

/*
 * IRIG format frames (1000 Hz, 1 second for 10 frames of data)
 */

/*
 * IRIG format frame 10 - MS straight binary seconds
 */
struct progx progu[] = {
    {COEF, 2},   /* 0 0x0 0200 seconds */
    {COEF, 4},   /* 1 0x0 0400 */
    {COEF, 8},   /* 2 0x0 0800 */
    {DECC, 1},   /* 3 0x0 1000 */
    {COEF, 2},   /* 4 0x0 2000 */
    {COEF, 4},   /* 6 0x0 4000 */
    {COEF, 8},   /* 7 0x0 8000 */
    {DECC, 1},   /* 8 0x1 0000 */
    {COEF, 2},   /* 9 0x2 0000 - but only 86,401 / 0x1 5181 seconds in a day, so
                    always zero */
    {NODEC, M8}, /* 9 PI */
};

/*
 * IRIG format frame 8 - MS control functions
 */
struct progx progv[] = {
    {COEF, 2}, /*  0 CF # 19 */
    {COEF, 4}, /*  1 CF # 20 */
    {COEF, 8}, /*  2 CF # 21 */
    {DECC, 1}, /*  3 CF # 22 */
    {COEF, 2}, /*  4 CF # 23 */
    {COEF, 4}, /*  6 CF # 24 */
    {COEF, 8}, /*  7 CF # 25 */
    {DECC, 1}, /*  8 CF # 26 */
    {COEF, 2}, /*  9 CF # 27 */
    {DEC, M8}, /* 10 PI */
};

/*
 * IRIG format frames 7 & 9 - LS control functions & LS straight binary seconds
 */
struct progx progw[] = {
    {COEF, 1},   /*  0  CF # 10, 0x0 0001 seconds */
    {COEF, 2},   /*  1  CF # 11, 0x0 0002 */
    {COEF, 4},   /*  2  CF # 12, 0x0 0004 */
    {COEF, 8},   /*  3  CF # 13, 0x0 0008 */
    {DECC, 1},   /*  4  CF # 14, 0x0 0010 */
    {COEF, 2},   /*  6  CF # 15, 0x0 0020 */
    {COEF, 4},   /*  7  CF # 16, 0x0 0040 */
    {COEF, 8},   /*  8  CF # 17, 0x0 0080 */
    {DECC, 1},   /*  9  CF # 18, 0x0 0100 */
    {NODEC, M8}, /* 10  PI */
};

/*
 * IRIG format frames 2 to 6 - minutes, hours, days, hundreds days, 2 digit
 * years (also called control functions bits 1-9)
 */
struct progx progy[] = {
    {COEF, 1},  /* 0 1 units, CF # 1 */
    {COEF, 2},  /* 1 2 units, CF # 2 */
    {COEF, 4},  /* 2 4 units, CF # 3 */
    {COEF, 8},  /* 3 8 units, CF # 4 */
    {DECZ, M2}, /* 4 zero bit, CF # 5 / unused, default zero in years */
    {COEF, 1},  /* 5 10 tens, CF # 6 */
    {COEF, 2},  /* 6 20 tens, CF # 7*/
    {COEF, 4},  /* 7 40 tens, CF # 8*/
    {COEF, 8},  /* 8 80 tens, CF # 9*/
    {DEC, M8},  /* 9 PI */
};

/*
 * IRIG format first frame, frame 1 - seconds
 */
struct progx progz[] = {
    {MIN, M8},  /* 0 PI (on-time marker for the second at zero cross of 1st cycle) */
    {COEF, 1},  /* 1 1 units */
    {COEF, 2},  /* 2 2 */
    {COEF, 4},  /* 3 4 */
    {COEF, 8},  /* 4 8 */
    {DECZ, M2}, /* 5 zero bit */
    {COEF, 1},  /* 6 10 tens */
    {COEF, 2},  /* 7 20 */
    {COEF, 4},  /* 8 40 */
    {DEC, M8},  /* 9 PI */
};

/* LeapState values. */
#define LEAPSTATE_NORMAL (0)
#define LEAPSTATE_DELETING (1)
#define LEAPSTATE_INSERTING (2)
#define LEAPSTATE_ZERO_AFTER_INSERT (3)

/*
 * Forward declarations
 */
void WWV_Second(int, int);                     /* send second */
void WWV_SecondNoTick(int, int);               /* send second with no tick */
void digit(int);                               /* encode digit */
void peep(int, int, int);                      /* send cycles */
int ConvertMonthDayToDayOfYear(int, int, int); /* Calc day of year from year month & day */
void Help(void);                               /* Usage message */
void ReverseString(char *);
void Delay(long ms);
size_t strlcat(char *dst, const char *src, size_t size);


/*
 * Extern declarations, don't know why not in headers
 */
// float	round ( float );

/*
 * Global variables
 */
char buffer[BUFLNG];         /* output buffer */
int bufcnt = 0;              /* buffer counter */
int tone = 1000;             /* WWV sync frequency */
int HourTone = 1500;         /* WWV hour on-time frequency */
int encode = IRIG;           /* encoder select */
int leap = 0;                /* leap indicator */
int DstFlag = 0;             /* winter/summer time */
int dut1 = 0;                /* DUT1 correction (sign, magnitude) */
int utc = 0;                 /* option epoch */
int IrigIncludeYear = FALSE; /* Whether to send year in first control functions
                                area, between P5 and P6. */
int IrigIncludeIeee = FALSE; /* Whether to send IEEE 1344 control functions
                                extensions between P6 and P8. */
int StraightBinarySeconds = 0;
int ControlFunctions = 0;
int Debug = FALSE;
int Verbose = TRUE;
char *CommandName;
PaStream *stream = NULL;

int TotalSecondsCorrected = 0;
int TotalCyclesAdded = 0;
int TotalCyclesRemoved = 0;

double SampleRate;
int AudioDelayMs = 17; /* my usb dongle, maybe not your codec */

void Die(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);
  vfprintf(stderr, fmt, vargs);
  va_end(vargs);
  fprintf(stderr, "\n");
  Pa_Terminate();
  exit(1);
}

/*
 * Main program
 */
int main(int argc, char **argv) {
  struct timeval TimeValue;              /* System clock at startup */
  time_t SecondsPartOfTime;              /* Sent to gmtime() for calculation of TimeStructure
                                            (can apply offset). */
  time_t BaseRealTime;                   /* Base realtime so can determine seconds since starting. */
  time_t NowRealTime;                    /* New realtime to can determine seconds as of now. */
  unsigned SecondsRunningRealTime;       /* Difference between NowRealTime and
                                            BaseRealTime. */
  unsigned SecondsRunningSimulationTime; /* Time that the simulator has been
                                            running. */
  int SecondsRunningDifference;          /* Difference between what real time says we
                                            have been running */
  /* and what simulator says we have been running - will slowly  */
  /* change because of clock drift. */
  int ExpectedRunningDifference = 0;     /* Stable value that we've obtained from
                                            check at initial start-up.	*/
  unsigned StabilityCount;               /* Used to check stability of difference while starting */
#define RUN_BEFORE_STABILITY_CHECK (30)  // Must run this many seconds before even checking stability.
#define MINIMUM_STABILITY_COUNT \
  (10)  // Number of consecutive differences that need to be within initial
        // stability band to say we are stable.
#define INITIAL_STABILITY_BAND \
  (2)  // Determining initial stability for consecutive differences within +/-
       // this value.
#define RUNNING_STABILITY_BAND \
  (5)  // When running, stability is defined as difference within +/- this
       // value.

  struct tm *TimeStructure = NULL; /* Structure returned by gmtime */
  char code[200];                  /* timecode */
  int temp;
  int arg = 0;
  int sw = 0;
  int ptr = 0;

  int Year;
  int Month;
  int DayOfMonth;
  int Hour;
  int Minute;
  int Second = 0;
  int DayOfYear;

  int BitNumber;
  char FormatCharacter = '3'; /* Default is IRIG-B with IEEE 1344 extensions */
  char AsciiValue;
  int HexValue;
  // int	OldPtr = 0;
  int FrameNumber = 0;

  /* Time offset for IEEE 1344 indication. */
  float TimeOffset = 0.0;
  int OffsetSignBit = 0;
  int OffsetOnes = 0;
  int OffsetHalf = 0;

  unsigned int TimeQuality = 0; /* Time quality for IEEE 1344 indication. */
  char ParityString[200];       /* Partial output string, to calculate parity on. */
  int ParitySum = 0;
  int ParityValue;
  char *StringPointer;

  /* Flags to indicate requested leap second addition or deletion by command
   * line option. */
  /* Should be mutually exclusive - generally ensured by code which interprets
   * command line option. */
  int InsertLeapSecond = FALSE;
  int DeleteLeapSecond = FALSE;

  /* Date and time of requested leap second addition or deletion. */
  int LeapYear = 0;
  int LeapMonth = 0;
  int LeapDayOfMonth = 0;
  int LeapHour = 0;
  int LeapMinute = 0;
  int LeapDayOfYear = 0;

  /* State flag for the insertion and deletion of leap seconds, esp. deletion,
   */
  /* where the logic gets a bit tricky. */
  int LeapState = LEAPSTATE_NORMAL;

  /* Flags for indication of leap second pending and leap secod polarity in IEEE
   * 1344 */
  int LeapSecondPending = FALSE;
  int LeapSecondPolarity = FALSE;

  /* Date and time of requested switch into or out of DST by command line
   * option. */
  int DstSwitchYear = 0;
  int DstSwitchMonth = 0;
  int DstSwitchDayOfMonth = 0;
  int DstSwitchHour = 0;
  int DstSwitchMinute = 0;
  int DstSwitchDayOfYear = 0;

  /* Indicate when we have been asked to switch into or out of DST by command
   * line option. */
  int DstSwitchFlag = FALSE;

  /* To allow predict for DstPendingFlag in IEEE 1344 */
  int DstSwitchPendingYear = 0; /* Default value isn't valid, but I don't care. */
  int DstSwitchPendingDayOfYear = 0;
  int DstSwitchPendingHour = 0;
  int DstSwitchPendingMinute = 0;

  /* /Flag for indication of a DST switch pending in IEEE 1344 */
  int DstPendingFlag = FALSE;

  /* Offset to actual time value sent. */
  float UseOffsetHoursFloat;
  int UseOffsetSecondsInt = 0;
  float UseOffsetSecondsFloat;

  /* String to allow us to put out reversed data - so can read the binary
   * numbers. */
  char OutputDataString[OUTPUT_DATA_STRING_LENGTH];

  /* Number of seconds to send before exiting.  Default = 0 = forever. */
  int SecondsToSend = 0;
  int CountOfSecondsSent = 0; /* Counter of seconds */

  /* Flags to indicate whether to add or remove a cycle for time adjustment. */
  int AddCycle = FALSE;     // We are ahead, add cycle to slow down and get back in sync.
  int RemoveCycle = FALSE;  // We are behind, remove cycle to slow down and get back in sync.
  int RateCorrection;       // Aggregate flag for passing to subroutines.
  int EnableRateCorrection = TRUE;
  char deviceNumOrName[512] = {0};
  float DesiredSampleRate = -1;

  float RatioError;

  CommandName = argv[0];

  if (argc < 1) {
    Help();
    exit(-1);
  }

  /*
   * Parse options
   */
  Year = 0;

  while ((temp = getopt(argc, argv, "a:b:c:dD:f:g:hHi:jk:l:o:q:r:stu:xy:z?")) != -1) {
    switch (temp) {
      case 'a':
        strncpy(deviceNumOrName, optarg, sizeof deviceNumOrName);
        break;

      case 'b': /* Remove (delete) a leap second at the end of the specified
                   minute. */
        sscanf(optarg, "%2d%2d%2d%2d%2d", &LeapYear, &LeapMonth, &LeapDayOfMonth, &LeapHour, &LeapMinute);
        InsertLeapSecond = FALSE;
        DeleteLeapSecond = TRUE;
        break;

      case 'c': /* specify number of seconds to send output for before exiting,
                   0 = forever */
        sscanf(optarg, "%d", &SecondsToSend);
        break;

      case 'd': /* set DST for summer (WWV/H only) / start with DST active
                   (IRIG) */
        DstFlag++;
        break;

      case 'D': /* path dealy through audio system */
        sscanf(optarg, "%d", &AudioDelayMs);
        break;

      case 'f': /* select format: i=IRIG-98 (default) 2=IRIG-2004
                   3-IRIG+IEEE-1344 w=WWV(H) */
        sscanf(optarg, "%c", &FormatCharacter);
        break;

      case 'g': /* Date and time to switch back into / out of DST active. */
        sscanf(optarg, "%2d%2d%2d%2d%2d", &DstSwitchYear, &DstSwitchMonth, &DstSwitchDayOfMonth, &DstSwitchHour,
               &DstSwitchMinute);
        DstSwitchFlag = TRUE;
        break;

      case 'h':
      case 'H':
      case '?':
        Help();
        exit(-1);
        break;

      case 'i': /* Insert (add) a leap second at the end of the specified
                   minute. */
        sscanf(optarg, "%2d%2d%2d%2d%2d", &LeapYear, &LeapMonth, &LeapDayOfMonth, &LeapHour, &LeapMinute);
        InsertLeapSecond = TRUE;
        DeleteLeapSecond = FALSE;
        break;

      case 'j':
        EnableRateCorrection = FALSE;
        break;

      case 'k':
        sscanf(optarg, "%d", &RateCorrection);
        EnableRateCorrection = FALSE;
        if (RateCorrection < 0) {
          RemoveCycle = TRUE;
          AddCycle = FALSE;

          if (Verbose) printf("\n> Forcing rate correction removal of cycle...\n");
        } else {
          if (RateCorrection > 0) {
            RemoveCycle = FALSE;
            AddCycle = TRUE;

            if (Verbose) printf("\n> Forcing rate correction addition of cycle...\n");
          }
        }
        break;

      case 'l': /* use time offset from UTC */
        sscanf(optarg, "%f", &UseOffsetHoursFloat);
        UseOffsetSecondsFloat = UseOffsetHoursFloat * (float)SECONDS_PER_HOUR;
        UseOffsetSecondsInt = (int)(UseOffsetSecondsFloat + 0.5);
        break;

      case 'o': /* Set IEEE 1344 time offset in hours - positive or negative, to
                   the half hour */
        sscanf(optarg, "%f", &TimeOffset);
        if (TimeOffset >= -0.2) {
          OffsetSignBit = 0;

          if (TimeOffset > 0) {
            OffsetOnes = TimeOffset;

            if ((TimeOffset - floor(TimeOffset)) >= 0.4)
              OffsetHalf = 1;
            else
              OffsetHalf = 0;
          } else {
            OffsetOnes = 0;
            OffsetHalf = 0;
          }
        } else {
          OffsetSignBit = 1;
          OffsetOnes = -TimeOffset;

          if ((ceil(TimeOffset) - TimeOffset) >= 0.4)
            OffsetHalf = 1;
          else
            OffsetHalf = 0;
        }
        break;

      case 'q': /* Hex quality code 0 to 0x0F - 0 = maximum, 0x0F = no lock */
        sscanf(optarg, "%x", &TimeQuality);
        TimeQuality &= 0x0F;
        break;

      case 'r':
        sscanf(optarg, "%f", &DesiredSampleRate);
        break;

      case 's': /* set leap warning bit (WWV/H only) */
        leap++;
        break;

      case 't': /* select WWVH sync frequency */
        tone = 1200;
        break;

      case 'u': /* set DUT1 offset (-7 to +7) */
        sscanf(optarg, "%d", &dut1);
        if (dut1 < 0)
          dut1 = abs(dut1);
        else
          dut1 |= 0x8;
        break;

      case 'x': /* Turn off verbose output. */
        Verbose = FALSE;
        break;

      case 'y': /* Set initial date and time */
        sscanf(optarg, "%2d%2d%2d%2d%2d%2d", &Year, &Month, &DayOfMonth, &Hour, &Minute, &Second);
        utc++;
        break;

      case 'z': /* Turn on Debug output (also turns on Verbose below) */
        Debug = TRUE;
        break;

      default:
        printf("Invalid option \"%c\", aborting...\n", temp);
        exit(-1);
        break;
    }
  }

  if (Debug) Verbose = TRUE;

  if (InsertLeapSecond || DeleteLeapSecond) {
    LeapDayOfYear = ConvertMonthDayToDayOfYear(LeapYear, LeapMonth, LeapDayOfMonth);

    if (Debug) {
      printf(
          "\nHave request for leap second %s at year %4d day %3d at "
          "%2.2dh%2.2d....\n",
          DeleteLeapSecond ? "DELETION" : (InsertLeapSecond ? "ADDITION" : "( error ! )"), LeapYear, LeapDayOfYear,
          LeapHour, LeapMinute);
    }
  }

  if (DstSwitchFlag) {
    DstSwitchDayOfYear = ConvertMonthDayToDayOfYear(DstSwitchYear, DstSwitchMonth, DstSwitchDayOfMonth);

    /* Figure out time of minute previous to DST switch, so can put up warning
     * flag in IEEE 1344 */
    DstSwitchPendingYear = DstSwitchYear;
    DstSwitchPendingDayOfYear = DstSwitchDayOfYear;
    DstSwitchPendingHour = DstSwitchHour;
    DstSwitchPendingMinute = DstSwitchMinute - 1;
    if (DstSwitchPendingMinute < 0) {
      DstSwitchPendingMinute = 59;
      DstSwitchPendingHour--;
      if (DstSwitchPendingHour < 0) {
        DstSwitchPendingHour = 23;
        DstSwitchPendingDayOfYear--;
        if (DstSwitchPendingDayOfYear < 1) {
          DstSwitchPendingYear--;
        }
      }
    }

    if (Debug) {
      printf("\nHave DST switch request for year %4d day %3d at %2.2dh%2.2d,", DstSwitchYear, DstSwitchDayOfYear,
             DstSwitchHour, DstSwitchMinute);
      printf("\n    so will have warning at year %4d day %3d at %2.2dh%2.2d.\n", DstSwitchPendingYear,
             DstSwitchPendingDayOfYear, DstSwitchPendingHour, DstSwitchPendingMinute);
    }
  }

  switch (tolower(FormatCharacter)) {
    case 'i':
      printf("\nFormat is IRIG-1998 (no year coded)...\n\n");
      encode = IRIG;
      IrigIncludeYear = FALSE;
      IrigIncludeIeee = FALSE;
      break;

    case '2':
      printf("\nFormat is IRIG-2004 (BCD year coded)...\n\n");
      encode = IRIG;
      IrigIncludeYear = TRUE;
      IrigIncludeIeee = FALSE;
      break;

    case '3':
      printf(
          "\nFormat is IRIG with IEEE-1344 (BCD year coded, and more control "
          "functions)...\n\n");
      encode = IRIG;
      IrigIncludeYear = TRUE;
      IrigIncludeIeee = TRUE;
      break;

    case 'w':
      printf("\nFormat is WWV(H)...\n\n");
      encode = WWV;
      break;

    default:
      printf(
          "\n\nUnexpected format value of \'%c\', cannot parse, "
          "aborting...\n\n",
          FormatCharacter);
      exit(-1);
      break;
  }

  /*
   * Open audio device and set options
   */

  PaError err;
  err = Pa_Initialize();
  if (err != paNoError) Die("Pa_Initialize failed: %s\n", Pa_GetErrorText(err));

  int numDevices = Pa_GetDeviceCount();
  if (numDevices < 0) Die("no audio devices");

  int deviceNum = -1;
  for (int i = 0; i < numDevices; i++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(i);
    printf("%02d: %s\n", i, deviceInfo->name);
    if (strcmp(deviceNumOrName, deviceInfo->name) == 0) {
      deviceNum = i;
    }
  }

  if (deviceNum < 0) {
    if (*deviceNumOrName) {
      sscanf(deviceNumOrName, "%d", &deviceNum);
    } else {
      deviceNum = Pa_GetDefaultOutputDevice();
      if (deviceNum == paNoDevice) Die("No default output device");
    }
  }

  if (deviceNum < 0 || deviceNum >= numDevices) Die("Can't find device (bad device specification).");

  const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(deviceNum);
  printf("using device %s\n", deviceInfo->name);
  if (DesiredSampleRate > 0.0) {
    printf("desired sample rate=%f\n", DesiredSampleRate);
    SampleRate = DesiredSampleRate;
  } else {
    // 44.1 KHz is most common but does not work.  Most devices support 48KHz.
    SampleRate = 48000.;
  }

  PaStreamParameters outputParameters;
  memset(&outputParameters, 0, sizeof outputParameters);
  outputParameters.device = deviceNum;
  outputParameters.channelCount = 1;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;

  err = Pa_IsFormatSupported(NULL, &outputParameters, SampleRate);
  if (err != paFormatIsSupported) Die("Audio output format is not supported.");

  err = Pa_OpenStream(&stream, NULL, /* no input */
                      &outputParameters, SampleRate, BUFLNG,
                      paClipOff, /* we won't output out of range samples so don't bother clipping them */
                      NULL,      /* no callback, use blocking API */
                      NULL);     /* no callback, so no callback userData */
  if (err != paNoError) Die("Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));

  const PaStreamInfo *info = Pa_GetStreamInfo(stream);
  if (info == NULL) Die("failed to get stream info");
  printf("sample rate=%f\n", info->sampleRate);

  printf("Starting stream\n");
  err = Pa_StartStream(stream);
  if (err != paNoError) Die("Pa_StartStream failed: %s\n", Pa_GetErrorText(err));

  /*
   * Unless specified otherwise, read the system clock and
   * initialize the time.
   */
  gettimeofday(&TimeValue, NULL);  // Now always read the system time to keep
                                   // "real time" of operation.
  NowRealTime = BaseRealTime = SecondsPartOfTime = TimeValue.tv_sec;
  SecondsRunningSimulationTime = 0;  // Just starting simulation, running zero seconds as of now.
  StabilityCount = 0;                // No stability yet.

  if (utc) {
    DayOfYear = ConvertMonthDayToDayOfYear(Year, Month, DayOfMonth);
  } else {
    /* Apply offset to time. */
    if (UseOffsetSecondsInt >= 0)
      SecondsPartOfTime += (time_t)UseOffsetSecondsInt;
    else
      SecondsPartOfTime -= (time_t)(-UseOffsetSecondsInt);

    if ((AudioDelayMs > 200) || (AudioDelayMs < 0)) Die("Bad value for audio delay (%d)", AudioDelayMs);

    while (1) {
      TimeStructure = gmtime(&SecondsPartOfTime);
      Minute = TimeStructure->tm_min;
      Hour = TimeStructure->tm_hour;
      DayOfYear = TimeStructure->tm_yday + 1;
      Year = TimeStructure->tm_year % 100;
      Second = TimeStructure->tm_sec;

      int ms = (int)(1000L - (long)TimeValue.tv_usec / 1000L) /* ms */;
      ms -= AudioDelayMs;
      if (ms == 0) {
        break;
      }
      if (ms > 0) {
        Delay(ms);
        break;
      }
    }
  }

  StraightBinarySeconds = Second + (Minute * SECONDS_PER_MINUTE) + (Hour * SECONDS_PER_HOUR);

  memset(code, 0, sizeof(code));
  switch (encode) {
    /*
     * For WWV/H and default time, carefully set the signal
     * generator seconds number to agree with the current time.
     */
    case WWV:
      printf("WWV time signal, starting point:\n");
      printf(
          " Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Minute "
          "tone = %d Hz, Hour tone = %d Hz.\n",
          Year, DayOfYear, Hour, Minute, Second, tone, HourTone);
      snprintf(code, sizeof(code), "%01d%03d%02d%02d%01d", Year / 10, DayOfYear, Hour, Minute, Year % 10);
      if (Verbose) {
        printf(
            "\n Year = %2.2d, Day of year = %3d, Time = %2.2d:%2.2d:%2.2d, "
            "Code = %s",
            Year, DayOfYear, Hour, Minute, Second, code);

        if ((EnableRateCorrection) || (RemoveCycle) || (AddCycle))
          printf(
              ", CountOfSecondsSent = %d, TotalCyclesAdded = %d, "
              "TotalCyclesRemoved = %d\n",
              CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
        else
          printf("\n");
      }

      ptr = 8;
      for (BitNumber = 0; BitNumber <= Second; BitNumber++) {
        if (progx[BitNumber].sw == DEC) ptr--;
      }
      break;

    /*
     * For IRIG the signal generator runs every second, so requires
     * no additional alignment.
     */
    case IRIG:
      printf("IRIG-B time signal, starting point:\n");
      printf(
          " Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Straight "
          "binary seconds (SBS) = %05d / 0x%04X.\n",
          Year, DayOfYear, Hour, Minute, Second, StraightBinarySeconds, StraightBinarySeconds);
      printf("\n");
      if (Verbose) {
        printf(
            "Codes: \".\" = marker/position indicator, \"-\" = zero dummy bit, "
            "\"0\" = zero bit, \"1\" = one bit.\n");
        if ((EnableRateCorrection) || (AddCycle) || (RemoveCycle)) {
          printf(
              "       \"o\" = short zero, \"*\" = long zero, \"x\" = short "
              "one, \"+\" = long one.\n");
        }
        printf(
            "Numerical values are time order reversed in output to make it "
            "easier to read.\n");
        printf("\n");
        printf("Legend of output codes:\n");
      }
      break;
  }

  /*
   * Run the signal generator to generate new timecode strings
   * once per minute for WWV/H and once per second for IRIG.
   */
  for (CountOfSecondsSent = 0; ((SecondsToSend == 0) || (CountOfSecondsSent < SecondsToSend)); CountOfSecondsSent++) {
    if ((encode == IRIG) && (((Second % 20) == 0) || (CountOfSecondsSent == 0))) {
      printf("\n");

      printf(
          " Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Straight "
          "binary seconds (SBS) = %05d / 0x%04X.\n",
          Year, DayOfYear, Hour, Minute, Second, StraightBinarySeconds, StraightBinarySeconds);
      if ((EnableRateCorrection) || (RemoveCycle) || (AddCycle)) {
        printf(
            " CountOfSecondsSent = %d, TotalCyclesAdded = %d, "
            "TotalCyclesRemoved = %d\n",
            CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
        if ((CountOfSecondsSent != 0) && ((TotalCyclesAdded != 0) || (TotalCyclesRemoved != 0))) {
          RatioError = ((float)(TotalCyclesAdded - TotalCyclesRemoved)) / (1000.0 * (float)CountOfSecondsSent);
          printf(
              " Adjusted by %2.1f%%, apparent send frequency is %4.2f Hz not "
              "%.3f Hz.\n\n",
              RatioError * 100.0, (1.0 + RatioError) * SampleRate, SampleRate);
        }
      } else
        printf("\n");

      if (Verbose) {
        printf(
            "|  StraightBinSecs  | IEEE_1344_Control |   Year  |    Day_of_Year  "
            "  |  Hours  | Minutes |Seconds |\n");
        printf(
            "|  ---------------  | ----------------- |   ----  |    -----------  "
            "  |  -----  | ------- |------- |\n");
        printf(
            "|                   |                   |         |                 "
            "  |         |         |        |\n");
      }
    }

    if (RemoveCycle) {
      RateCorrection = -1;
      TotalSecondsCorrected++;
    } else {
      if (AddCycle) {
        TotalSecondsCorrected++;
        RateCorrection = +1;
      } else
        RateCorrection = 0;
    }

    /*
     * Crank the state machine to propagate carries to the
     * year of century. Note that we delayed up to one
     * second for alignment after reading the time, so this
     * is the next second.
     */

    if (LeapState == LEAPSTATE_NORMAL) {
      /* If on the second of a leap (second 59 in the specified minute), then
       * add or delete a second */
      if ((Year == LeapYear) && (DayOfYear == LeapDayOfYear) && (Hour == LeapHour) && (Minute == LeapMinute)) {
        /* To delete a second, which means we go from 58->60 instead of
         * 58->59->00. */
        if ((DeleteLeapSecond) && (Second == 58)) {
          LeapState = LEAPSTATE_DELETING;

          if (Debug) printf("\n<--- Ready to delete a leap second...\n");
        } else { /* Delete takes precedence over insert. */
          /* To add a second, which means we go from 59->60->00 instead of
           * 59->00. */
          if ((InsertLeapSecond) && (Second == 59)) {
            LeapState = LEAPSTATE_INSERTING;

            if (Debug) printf("\n<--- Ready to insert a leap second...\n");
          }
        }
      }
    }

    switch (LeapState) {
      case LEAPSTATE_NORMAL:
        Second = (Second + 1) % 60;
        break;

      case LEAPSTATE_DELETING:
        Second = 0;
        LeapState = LEAPSTATE_NORMAL;

        if (Debug) printf("\n<--- Deleting a leap second...\n");
        break;

      case LEAPSTATE_INSERTING:
        Second = 60;
        LeapState = LEAPSTATE_ZERO_AFTER_INSERT;

        if (Debug) printf("\n<--- Inserting a leap second...\n");
        break;

      case LEAPSTATE_ZERO_AFTER_INSERT:
        Second = 0;
        LeapState = LEAPSTATE_NORMAL;

        if (Debug) printf("\n<--- Inserted a leap second, now back to zero...\n");
        break;

      default:
        printf("\n\nLeap second state invalid value of %d, aborting...", LeapState);
        exit(-1);
        break;
    }

    /* Check for second rollover, increment minutes and ripple upward if
     * required. */
    if (Second == 0) {
      Minute++;
      if (Minute >= 60) {
        Minute = 0;
        Hour++;
      }

      /* Check for activation of DST switch. */
      /* If DST is active, this would mean that at the appointed time, we
       * de-activate DST, */
      /* which translates to going backward an hour (repeating the last hour).
       */
      /* If DST is not active, this would mean that at the appointed time, we
       * activate DST, */
      /* which translates to going forward an hour (skipping the next hour). */
      if (DstSwitchFlag) {
        /* The actual switch happens on the zero'th second of the actual minute
         * specified. */
        if ((Year == DstSwitchYear) && (DayOfYear == DstSwitchDayOfYear) && (Hour == DstSwitchHour) &&
            (Minute == DstSwitchMinute)) {
          if (DstFlag == 0) { /* DST flag is zero, not in DST, going to DST, "spring
                                 ahead", so increment hour by two instead of one. */
            Hour++;
            DstFlag = 1;

            /* Must adjust offset to keep consistent with UTC. */
            /* Here we have to increase offset by one hour.  If it goes from
             * negative to positive, then we fix that. */
            if (OffsetSignBit == 0) { /* Offset is positive */
              if (OffsetOnes == 0x0F) {
                OffsetSignBit = 1;
                OffsetOnes = (OffsetHalf == 0) ? 8 : 7;
              } else
                OffsetOnes++;
            } else { /* Offset is negative */
              if (OffsetOnes == 0) {
                OffsetSignBit = 0;
                OffsetOnes = (OffsetHalf == 0) ? 1 : 0;
              } else
                OffsetOnes--;
            }

            if (Debug)
              printf(
                  "\n<--- DST activated, spring ahead an hour, new offset "
                  "!...\n");
          } else { /* DST flag is non zero, in DST, going out of DST, "fall
                      back", so no increment of hour. */
            Hour--;
            DstFlag = 0;

            /* Must adjust offset to keep consistent with UTC. */
            /* Here we have to reduce offset by one hour.  If it goes negative,
             * then we fix that. */
            if (OffsetSignBit == 0) { /* Offset is positive */
              if (OffsetOnes == 0) {
                OffsetSignBit = 1;
                OffsetOnes = (OffsetHalf == 0) ? 1 : 0;
              } else
                OffsetOnes--;
            } else { /* Offset is negative */
              if (OffsetOnes == 0x0F) {
                OffsetSignBit = 0;
                OffsetOnes = (OffsetHalf == 0) ? 8 : 7;
              } else
                OffsetOnes++;
            }

            if (Debug) printf("\n<--- DST de-activated, fall back an hour!...\n");
          }

          DstSwitchFlag = FALSE; /* One time deal, not intended to run this
                                    program past two switches... */
        }
      }

      if (Hour >= 24) {
        /* Modified, just in case dumb case where activating DST advances
         * 23h59:59 -> 01h00:00 */
        Hour = Hour % 24;
        DayOfYear++;
      }

      /*
       * At year rollover check for leap second.
       */
      if (DayOfYear >= (Year & 0x3 ? 366 : 367)) {
        if (leap) {
          WWV_Second(DATA0, RateCorrection);
          if (Verbose) printf("\nLeap!");
          leap = 0;
        }
        DayOfYear = 1;
        Year++;
      }
      if (encode == WWV) {
        snprintf(code, sizeof(code), "%01d%03d%02d%02d%01d", Year / 10, DayOfYear, Hour, Minute, Year % 10);
        if (Verbose)
          printf(
              "\n Year = %2.2d, Day of year = %3d, Time = %2.2d:%2.2d:%2.2d, "
              "Code = %s",
              Year, DayOfYear, Hour, Minute, Second, code);

        if ((EnableRateCorrection) || (RemoveCycle) || (AddCycle)) {
          printf(
              ", CountOfSecondsSent = %d, TotalCyclesAdded = %d, "
              "TotalCyclesRemoved = %d\n",
              CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
          if ((CountOfSecondsSent != 0) && ((TotalCyclesAdded != 0) || (TotalCyclesRemoved != 0))) {
            RatioError = ((float)(TotalCyclesAdded - TotalCyclesRemoved)) / (1000.0 * (float)CountOfSecondsSent);
            printf(
                " Adjusted by %2.1f%%, apparent send frequency is %4.2f Hz not "
                "%.3f Hz.\n\n",
                RatioError * 100.0, (1.0 + RatioError) * SampleRate, SampleRate);
          }
        } else
          printf("\n");

        ptr = 8;
      }
    } /* End of "if  (Second == 0)" */

    /* After all that, if we are in the minute just prior to a leap second, warn
     * of leap second pending */
    /* and of the polarity */
    if ((Year == LeapYear) && (DayOfYear == LeapDayOfYear) && (Hour == LeapHour) && (Minute == LeapMinute)) {
      LeapSecondPending = TRUE;
      LeapSecondPolarity = DeleteLeapSecond;
    } else {
      LeapSecondPending = FALSE;
      LeapSecondPolarity = FALSE;
    }

    /* Notification through IEEE 1344 happens during the whole minute previous
     * to the minute specified. */
    /* The time of that minute has been previously calculated. */
    if ((Year == DstSwitchPendingYear) && (DayOfYear == DstSwitchPendingDayOfYear) && (Hour == DstSwitchPendingHour) &&
        (Minute == DstSwitchPendingMinute)) {
      DstPendingFlag = TRUE;
    } else {
      DstPendingFlag = FALSE;
    }

    StraightBinarySeconds = Second + (Minute * SECONDS_PER_MINUTE) + (Hour * SECONDS_PER_HOUR);

    if (encode == IRIG) {
      if (IrigIncludeIeee) {
        if ((OffsetOnes == 0) && (OffsetHalf == 0)) OffsetSignBit = 0;

        ControlFunctions = (LeapSecondPending == 0 ? 0x00000 : 0x00001) |
                           (LeapSecondPolarity == 0 ? 0x00000 : 0x00002) | (DstPendingFlag == 0 ? 0x00000 : 0x00004) |
                           (DstFlag == 0 ? 0x00000 : 0x00008) | (OffsetSignBit == 0 ? 0x00000 : 0x00010) |
                           ((OffsetOnes & 0x0F) << 5) | (OffsetHalf == 0 ? 0x00000 : 0x00200) |
                           ((TimeQuality & 0x0F) << 10);
      } else
        ControlFunctions = 0;

      if (IrigIncludeYear) {
        snprintf(ParityString, sizeof(ParityString), "%04X%02d%04d%02d%02d%02d", ControlFunctions & 0x7FFF, Year,
                 DayOfYear, Hour, Minute, Second);
      } else {
        snprintf(ParityString, sizeof(ParityString), "%04X%02d%04d%02d%02d%02d", ControlFunctions & 0x7FFF, 0,
                 DayOfYear, Hour, Minute, Second);
      }

      if (IrigIncludeIeee) {
        ParitySum = 0;
        for (StringPointer = ParityString; *StringPointer != NUL; StringPointer++) {
          switch (toupper(*StringPointer)) {
            case '1':
            case '2':
            case '4':
            case '8':
              ParitySum += 1;
              break;

            case '3':
            case '5':
            case '6':
            case '9':
            case 'A':
            case 'C':
              ParitySum += 2;
              break;

            case '7':
            case 'B':
            case 'D':
            case 'E':
              ParitySum += 3;
              break;

            case 'F':
              ParitySum += 4;
              break;
          }
        }

        if ((ParitySum & 0x01) == 0x01)
          ParityValue = 0x01;
        else
          ParityValue = 0;
      } else
        ParityValue = 0;

      ControlFunctions |= ((ParityValue & 0x01) << 14);

      if (IrigIncludeYear) {
        snprintf(code, sizeof(code),
                 /* YearDay HourMin Sec */
                 "%05X%05X%02d%04d%02d%02d%02d", StraightBinarySeconds, ControlFunctions, Year, DayOfYear, Hour, Minute,
                 Second);
      } else {
        snprintf(code, sizeof(code),
                 /* YearDay HourMin Sec */
                 "%05X%05X%02d%04d%02d%02d%02d", StraightBinarySeconds, ControlFunctions, 0, DayOfYear, Hour, Minute,
                 Second);
      }

      if (Debug)
        printf(
            "\nCode string: %s, ParityString = %s, ParitySum = 0x%2.2X, "
            "ParityValue = %d, DstFlag = %d...\n",
            code, ParityString, ParitySum, ParityValue, DstFlag);

      ptr = strlen(code) - 1;
      // OldPtr = 0;
    }

    /*
     * Generate data for the second
     */
    switch (encode) {
      /*
       * The IRIG second consists of 20 BCD digits of width-
       * modulateod pulses at 2, 5 and 8 ms and modulated 50
       * percent on the 1000-Hz carrier.
       */
      case IRIG:
        /* Initialize the output string */
        OutputDataString[0] = '\0';

        for (BitNumber = 0; BitNumber < 100; BitNumber++) {
          FrameNumber = (BitNumber / 10) + 1;
          switch (FrameNumber) {
            case 1:
              /* bits 0 to 9, first frame */
              sw = progz[BitNumber % 10].sw;
              arg = progz[BitNumber % 10].arg;
              break;

            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
              /* bits 10 to 59, second to sixth frame */
              sw = progy[BitNumber % 10].sw;
              arg = progy[BitNumber % 10].arg;
              break;

            case 7:
              /* bits 60 to 69, seventh frame */
              sw = progw[BitNumber % 10].sw;
              arg = progw[BitNumber % 10].arg;
              break;

            case 8:
              /* bits 70 to 79, eighth frame */
              sw = progv[BitNumber % 10].sw;
              arg = progv[BitNumber % 10].arg;
              break;

            case 9:
              /* bits 80 to 89, ninth frame */
              sw = progw[BitNumber % 10].sw;
              arg = progw[BitNumber % 10].arg;
              break;

            case 10:
              /* bits 90 to 99, tenth frame */
              sw = progu[BitNumber % 10].sw;
              arg = progu[BitNumber % 10].arg;
              break;

            default:
              /* , Unexpected values of FrameNumber */
              printf(
                  "\n\nUnexpected value of FrameNumber = %d, cannot parse, "
                  "aborting...\n\n",
                  FrameNumber);
              exit(-1);
              break;
          }

          switch (sw) {
            case DECC: /* decrement pointer and send bit. */
              ptr--;
              /* FALLTHRU */
            case COEF: /* send BCD bit */
              AsciiValue = toupper(code[ptr]);
              HexValue = isdigit(AsciiValue) ? AsciiValue - '0' : (AsciiValue - 'A') + 10;
              /* if  (Debug) {
                      if  (ptr != OldPtr) {
                      if  (Verbose)
                          printf("\n(%c->%X)", AsciiValue, HexValue);
                      OldPtr = ptr;
                      }
              }
              */
              // OK, adjust all unused bits in hundreds of days.
              if ((FrameNumber == 5) && ((BitNumber % 10) > 1)) {
                if (RateCorrection < 0) {  // Need to remove cycles to catch up.
                  if ((HexValue & arg) != 0) {
                    peep(M5, 1000, HIGH);
                    peep(M5 - 1, 1000, LOW);

                    TotalCyclesRemoved += 1;
                    strlcat(OutputDataString, "x", OUTPUT_DATA_STRING_LENGTH);
                  } else {
                    peep(M2, 1000, HIGH);
                    peep(M8 - 1, 1000, LOW);

                    TotalCyclesRemoved += 1;
                    strlcat(OutputDataString, "o", OUTPUT_DATA_STRING_LENGTH);
                  }
                }                            // End of true clause for "if  (RateCorrection < 0)"
                else {                       // Else clause for "if  (RateCorrection < 0)"
                  if (RateCorrection > 0) {  // Need to add cycles to slow back down.
                    if ((HexValue & arg) != 0) {
                      peep(M5, 1000, HIGH);
                      peep(M5 + 1, 1000, LOW);

                      TotalCyclesAdded += 1;
                      strlcat(OutputDataString, "+", OUTPUT_DATA_STRING_LENGTH);
                    } else {
                      peep(M2, 1000, HIGH);
                      peep(M8 + 1, 1000, LOW);

                      TotalCyclesAdded += 1;
                      strlcat(OutputDataString, "*", OUTPUT_DATA_STRING_LENGTH);
                    }
                  }       // End of true clause for "if  (RateCorrection > 0)"
                  else {  // Else clause for "if  (RateCorrection > 0)"
                    // Rate is OK, just do what you feel!
                    if ((HexValue & arg) != 0) {
                      peep(M5, 1000, HIGH);
                      peep(M5, 1000, LOW);
                      strlcat(OutputDataString, "1", OUTPUT_DATA_STRING_LENGTH);
                    } else {
                      peep(M2, 1000, HIGH);
                      peep(M8, 1000, LOW);
                      strlcat(OutputDataString, "0", OUTPUT_DATA_STRING_LENGTH);
                    }
                  }   // End of else clause for "if  (RateCorrection > 0)"
                }     // End of else claues for "if  (RateCorrection < 0)"
              }       // End of true clause for "if  ((FrameNumber == 5) &&
                      // (BitNumber == 8))"
              else {  // Else clause for "if  ((FrameNumber == 5) && (BitNumber
                      // == 8))"
                if ((HexValue & arg) != 0) {
                  peep(M5, 1000, HIGH);
                  peep(M5, 1000, LOW);
                  strlcat(OutputDataString, "1", OUTPUT_DATA_STRING_LENGTH);
                } else {
                  peep(M2, 1000, HIGH);
                  peep(M8, 1000, LOW);
                  strlcat(OutputDataString, "0", OUTPUT_DATA_STRING_LENGTH);
                }
              }  // end of else clause for "if  ((FrameNumber == 5) &&
                 // (BitNumber == 8))"
              break;

            case DECZ: /* decrement pointer and send zero bit */
              ptr--;
              peep(M2, 1000, HIGH);
              peep(M8, 1000, LOW);
              strlcat(OutputDataString, "-", OUTPUT_DATA_STRING_LENGTH);
              break;

            case DEC: /* send marker/position indicator IM/PI bit */
              ptr--;
              /* FALLTHRU */
            case NODEC: /* send marker/position indicator IM/PI bit but no
                           decrement pointer */
            case MIN:   /* send "second start" marker/position indicator IM/PI bit
                         */
              peep(arg, 1000, HIGH);
              peep(10 - arg, 1000, LOW);
              strlcat(OutputDataString, ".", OUTPUT_DATA_STRING_LENGTH);
              break;

            default:
              printf(
                  "\n\nUnknown state machine value \"%d\", unable to continue, "
                  "aborting...\n\n",
                  sw);
              exit(-1);
              break;
          }
          if (ptr < 0) break;
        }
        ReverseString(OutputDataString);
        if (Verbose) {
          printf("%s", OutputDataString);
          if (RateCorrection > 0)
            printf(" fast\n");
          else {
            if (RateCorrection < 0)
              printf(" slow\n");
            else
              printf("\n");
          }
        }
        break;

      /*
       * The WWV/H second consists of 9 BCD digits of width-
       * modulateod pulses 200, 500 and 800 ms at 100-Hz.
       */
      case WWV:
        sw = progx[Second].sw;
        arg = progx[Second].arg;
        switch (sw) {
          case DATA: /* send data bit */
            WWV_Second(arg, RateCorrection);
            if (Verbose) {
              if (arg == DATA0)
                printf("0");
              else {
                if (arg == DATA1)
                  printf("1");
                else {
                  if (arg == PI)
                    printf("P");
                  else
                    printf("?");
                }
              }
            }
            break;

          case DATAX: /* send data bit */
            WWV_SecondNoTick(arg, RateCorrection);
            if (Verbose) {
              if (arg == DATA0)
                printf("0");
              else {
                if (arg == DATA1)
                  printf("1");
                else {
                  if (arg == PI)
                    printf("P");
                  else
                    printf("?");
                }
              }
            }
            break;

          case COEF: /* send BCD bit */
            if (code[ptr] & arg) {
              WWV_Second(DATA1, RateCorrection);
              if (Verbose) printf("1");
            } else {
              WWV_Second(DATA0, RateCorrection);
              if (Verbose) printf("0");
            }
            break;

          case LEAP: /* send leap bit */
            if (leap) {
              WWV_Second(DATA1, RateCorrection);
              if (Verbose) printf("L");
            } else {
              WWV_Second(DATA0, RateCorrection);
              if (Verbose) printf("0");
            }
            break;

          case DEC: /* send data bit */
            ptr--;
            WWV_Second(arg, RateCorrection);
            if (Verbose) {
              if (arg == DATA0)
                printf("0");
              else {
                if (arg == DATA1)
                  printf("1");
                else {
                  if (arg == PI)
                    printf("P");
                  else
                    printf("?");
                }
              }
            }
            break;

          case DECX: /* send data bit with no tick */
            ptr--;
            WWV_SecondNoTick(arg, RateCorrection);
            if (Verbose) {
              if (arg == DATA0)
                printf("0");
              else {
                if (arg == DATA1)
                  printf("1");
                else {
                  if (arg == PI)
                    printf("P");
                  else
                    printf("?");
                }
              }
            }
            break;

          case MIN: /* send minute sync */
            if (Minute == 0) {
              peep(arg, HourTone, HIGH);

              if (RateCorrection < 0) {
                peep(990 - arg, HourTone, OFF);
                TotalCyclesRemoved += 10;

                if (Debug) printf("\n* Shorter Second: ");
              } else {
                if (RateCorrection > 0) {
                  peep(1010 - arg, HourTone, OFF);

                  TotalCyclesAdded += 10;

                  if (Debug) printf("\n* Longer Second: ");
                } else {
                  peep(1000 - arg, HourTone, OFF);
                }
              }

              if (Verbose) printf("H");
            } else {
              peep(arg, tone, HIGH);

              if (RateCorrection < 0) {
                peep(990 - arg, tone, OFF);
                TotalCyclesRemoved += 10;

                if (Debug) printf("\n* Shorter Second: ");
              } else {
                if (RateCorrection > 0) {
                  peep(1010 - arg, tone, OFF);

                  TotalCyclesAdded += 10;

                  if (Debug) printf("\n* Longer Second: ");
                } else {
                  peep(1000 - arg, tone, OFF);
                }
              }

              if (Verbose) printf("M");
            }
            break;

          case DUT1: /* send DUT1 bits */
            if (dut1 & arg) {
              WWV_Second(DATA1, RateCorrection);
              if (Verbose) printf("1");
            } else {
              WWV_Second(DATA0, RateCorrection);
              if (Verbose) printf("0");
            }
            break;

          case DST1: /* send DST1 bit */
            ptr--;
            if (DstFlag) {
              WWV_Second(DATA1, RateCorrection);
              if (Verbose) printf("1");
            } else {
              WWV_Second(DATA0, RateCorrection);
              if (Verbose) printf("0");
            }
            break;

          case DST2: /* send DST2 bit */
            if (DstFlag) {
              WWV_Second(DATA1, RateCorrection);
              if (Verbose) printf("1");
            } else {
              WWV_Second(DATA0, RateCorrection);
              if (Verbose) printf("0");
            }
            break;
        }
    }

    if (EnableRateCorrection) {
      SecondsRunningSimulationTime++;

      gettimeofday(&TimeValue, NULL);
      NowRealTime = TimeValue.tv_sec;

      if (NowRealTime >= BaseRealTime)  // Just in case system time corrects
                                        // backwards, do not blow up.
      {
        SecondsRunningRealTime = (unsigned)(NowRealTime - BaseRealTime);
        SecondsRunningDifference = SecondsRunningSimulationTime - SecondsRunningRealTime;

        if (Debug) {
          printf(
              "> NowRealTime = 0x%8.8X, BaseRealtime = 0x%8.8X, "
              "SecondsRunningRealTime = 0x%8.8X, SecondsRunningSimulationTime "
              "= 0x%8.8X.\n",
              (unsigned)NowRealTime, (unsigned)BaseRealTime, SecondsRunningRealTime, SecondsRunningSimulationTime);
          printf(
              "> SecondsRunningDifference = 0x%8.8X, ExpectedRunningDifference "
              "= 0x%8.8X.\n",
              SecondsRunningDifference, ExpectedRunningDifference);
        }

        if (SecondsRunningSimulationTime > RUN_BEFORE_STABILITY_CHECK) {
          if (StabilityCount < MINIMUM_STABILITY_COUNT) {
            if (StabilityCount == 0) {
              ExpectedRunningDifference = SecondsRunningDifference;
              StabilityCount++;
              if (Debug) printf("> Starting stability check.\n");
            } else {  // Else for "if  (StabilityCount == 0)"
              if ((ExpectedRunningDifference + INITIAL_STABILITY_BAND > SecondsRunningDifference) &&
                  (ExpectedRunningDifference - INITIAL_STABILITY_BAND <
                   SecondsRunningDifference)) {  // So far, still within
                                                 // stability band, increment
                                                 // count.
                StabilityCount++;
                if (Debug) printf("> StabilityCount = %d.\n", StabilityCount);
              } else {  // Outside of stability band, start over.
                StabilityCount = 0;
                if (Debug) printf("> Out of stability band, start over.\n");
              }
            }     // End of else for "if  (StabilityCount == 0)"
          }       // End of true clause for "if  (StabilityCount <
                  // MINIMUM_STABILITY_COUNT))"
          else {  // Else clause for "if  (StabilityCount <
                  // MINIMUM_STABILITY_COUNT))" - OK, so we are supposed to be
                  // stable.
            if (AddCycle) {
              if (ExpectedRunningDifference >= SecondsRunningDifference) {
                if (Debug)
                  printf(
                      "> Was adding cycles, ExpectedRunningDifference >= "
                      "SecondsRunningDifference, can stop it now.\n");

                AddCycle = FALSE;
                RemoveCycle = FALSE;
              } else {
                if (Debug) printf("> Was adding cycles, not done yet.\n");
              }
            } else {
              if (RemoveCycle) {
                if (ExpectedRunningDifference <= SecondsRunningDifference) {
                  if (Debug)
                    printf(
                        "> Was removing cycles, ExpectedRunningDifference <= "
                        "SecondsRunningDifference, can stop it now.\n");

                  AddCycle = FALSE;
                  RemoveCycle = FALSE;
                } else {
                  if (Debug) printf("> Was removing cycles, not done yet.\n");
                }
              } else {
                if ((ExpectedRunningDifference + RUNNING_STABILITY_BAND > SecondsRunningDifference) &&
                    (ExpectedRunningDifference - RUNNING_STABILITY_BAND <
                     SecondsRunningDifference)) {  // All is well, within
                                                   // tolerances.
                  if (Debug) printf("> All is well, within tolerances.\n");
                } else {  // Oops, outside tolerances.  Else clause of "if
                          // ((ExpectedRunningDifference...SecondsRunningDifference)"
                  if (ExpectedRunningDifference > SecondsRunningDifference) {
                    if (Debug)
                      printf(
                          "> ExpectedRunningDifference > "
                          "SecondsRunningDifference, running behind real "
                          "time.\n");

                    // Behind real time, have to add a cycle to slow down and
                    // get back in sync.
                    AddCycle = FALSE;
                    RemoveCycle = TRUE;
                  } else {  // Else clause of "if  (ExpectedRunningDifference <
                            // SecondsRunningDifference)"
                    if (ExpectedRunningDifference < SecondsRunningDifference) {
                      if (Debug)
                        printf(
                            "> ExpectedRunningDifference < "
                            "SecondsRunningDifference, running ahead of real "
                            "time.\n");

                      // Ahead of real time, have to remove a cycle to speed up
                      // and get back in sync.
                      AddCycle = TRUE;
                      RemoveCycle = FALSE;
                    } else {
                      if (Debug)
                        printf(
                            "> Oops, outside tolerances, but doesn't fit the "
                            "profiles, how can this be?\n");
                    }
                  }  // End of else clause of "if  (ExpectedRunningDifference >
                     // SecondsRunningDifference)"
                }    // End of else clause of "if
                     // ((ExpectedRunningDifference...SecondsRunningDifference)"
              }      // End of else clause of "if  (RemoveCycle)".
            }        // End of else clause of "if  (AddCycle)".
          }          // End of else clause for "if  (StabilityCount <
                     // MINIMUM_STABILITY_COUNT))"
        }            // End of true clause for "if  ((SecondsRunningSimulationTime >
                     // RUN_BEFORE_STABILITY_CHECK)"
      }              // End of true clause for "if  (NowRealTime >= BaseRealTime)"
      else {
        if (Debug) printf("> Hmm, time going backwards?\n");
      }
    }  // End of true clause for "if  (EnableRateCorrection)"

    fflush(stdout);
  }

  printf("\n\n>> Completed %d seconds, exiting...\n\n", SecondsToSend);
  return (0);
}

void Delay(long ms) { peep(ms, 1000, OFF); }

/*
 * Generate WWV/H 0 or 1 data pulse.
 */
void WWV_Second(int code, /* DATA0, DATA1, PI */
                int Rate  /* <0 -> do a short second, 0 -> normal second, >0 ->
                             long second */
) {
  /*
   * The WWV data pulse begins with 5 ms of 1000 Hz follwed by a
   * guard time of 25 ms. The data pulse is 170, 570 or 770 ms at
   * 100 Hz corresponding to 0, 1 or position indicator (PI),
   * respectively. Note the 100-Hz data pulses are transmitted 6
   * dB below the 1000-Hz sync pulses. Originally the data pulses
   * were transmited 10 dB below the sync pulses, but the station
   * engineers increased that to 6 dB because the Heath GC-1000
   * WWV/H radio clock worked much better.
   */
  peep(5, tone, HIGH); /* send seconds tick */
  peep(25, tone, OFF);
  peep(code - 30, 100, LOW); /* send data */

  /* The quiet time is shortened or lengthened to get us back on time */
  if (Rate < 0) {
    peep(990 - code, 100, OFF);

    TotalCyclesRemoved += 10;

    if (Debug) printf("\n* Shorter Second: ");
  } else {
    if (Rate > 0) {
      peep(1010 - code, 100, OFF);

      TotalCyclesAdded += 10;

      if (Debug) printf("\n* Longer Second: ");
    } else
      peep(1000 - code, 100, OFF);
  }
}

/*
 * Generate WWV/H 0 or 1 data pulse, with no tick, for 29th and 59th seconds
 */
void WWV_SecondNoTick(int code, /* DATA0, DATA1, PI */
                      int Rate  /* <0 -> do a short second, 0 -> normal second,
                                   >0 -> long second */
) {
  /*
   * The WWV data pulse begins with 5 ms of 1000 Hz follwed by a
   * guard time of 25 ms. The data pulse is 170, 570 or 770 ms at
   * 100 Hz corresponding to 0, 1 or position indicator (PI),
   * respectively. Note the 100-Hz data pulses are transmitted 6
   * dB below the 1000-Hz sync pulses. Originally the data pulses
   * were transmited 10 dB below the sync pulses, but the station
   * engineers increased that to 6 dB because the Heath GC-1000
   * WWV/H radio clock worked much better.
   */
  peep(30, tone, OFF);       /* send seconds non-tick */
  peep(code - 30, 100, LOW); /* send data */

  /* The quiet time is shortened or lengthened to get us back on time */
  if (Rate < 0) {
    peep(990 - code, 100, OFF);

    TotalCyclesRemoved += 10;

    if (Debug) printf("\n* Shorter Second: ");
  } else {
    if (Rate > 0) {
      peep(1010 - code, 100, OFF);

      TotalCyclesAdded += 10;

      if (Debug) printf("\n* Longer Second: ");
    } else
      peep(1000 - code, 100, OFF);
  }
}

/*
 * Generate cycles of 100 Hz or any multiple of 100 Hz.
 */
void peep(int pulse, /* pulse length (ms) */
          int freq,  /* frequency (Hz) */
          int amp    /* amplitude */
) {
  double dpulse = pulse;
  double dfreq = freq;
  double damp;

  switch (amp) {
    case OFF:
      damp = 0.;
      break;
    case LOW:
      damp = 0.25;
      break;
    case HIGH:
      damp = 0.75;
      break;
    default:
      Die("???");
  }

  int n_samples = (int)(SampleRate * dpulse / 1000.);

  float *buffer;
  buffer = malloc(sizeof(float) * n_samples);

  for (int i = 0; i < n_samples; i++) {
    buffer[i] = damp * sin(dfreq * 2 * M_PI * ((double)i / SampleRate));
  }
  PaError err = Pa_WriteStream(stream, buffer, n_samples);
  free(buffer);
  switch (err) {
    case paOutputUnderflowed:
      printf("underflow... sadness\n");
      break;
    case paNoError:
      break;
    default:
      Die("failed to write to stream: %s\n", Pa_GetErrorText(err));
  }
}

/* Calc day of year from year month & day */
/* Year - 0 means 2000, 100 means 2100. */
/* Month - 1 means January, 12 means December. */
/* DayOfMonth - 1 is first day of month */
int ConvertMonthDayToDayOfYear(int YearValue, int MonthValue, int DayOfMonthValue) {
  int ReturnValue;
  int LeapYear;
  int MonthCounter;

  /* Array of days in a month.  Note that here January is zero. */
  /* NB: have to add 1 to days in February in a leap year! */
  int DaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  LeapYear = FALSE;
  if ((YearValue % 4) == 0) {
    if ((YearValue % 100) == 0) {
      if ((YearValue % 400) == 0) {
        LeapYear = TRUE;
      }
    } else {
      LeapYear = TRUE;
    }
  }

  if (Debug)
    printf("\nConvertMonthDayToDayOfYear(): Year %d %s a leap year.\n", YearValue + 2000, LeapYear ? "is" : "is not");

  /* Day of month given us starts in this algorithm. */
  ReturnValue = DayOfMonthValue;

  /* Add in days in month for each month past January. */
  for (MonthCounter = 1; MonthCounter < MonthValue; MonthCounter++) {
    ReturnValue += DaysInMonth[MonthCounter - 1];
  }

  /* Add a day for leap years where we are past February. */
  if ((LeapYear) && (MonthValue > 2)) {
    ReturnValue++;
  }

  if (Debug)
    printf(
        "\nConvertMonthDayToDayOfYear(): %4.4d-%2.2d-%2.2d represents day %3d "
        "of year.\n",
        YearValue + 2000, MonthValue, DayOfMonthValue, ReturnValue);

  return (ReturnValue);
}

void Help(void) {
  printf("\n\nTime Code Generation - IRIG-B or WWV, v%d.%d, %s dmw", VERSION, ISSUE, ISSUE_DATE);
  printf("\n\nRCS Info:");
  printf(
      "\n  $Header: /home/dmw/src/IRIG_generation/ntp-4.2.2p3/util/RCS/tg.c,v "
      "1.28 2007/02/12 23:57:45 dmw Exp $");
  printf("\n\nUsage: %s [option]*", CommandName);
  printf(
      "\n         -a name|N                      Audio device by name or "
      "number.");
  printf(
      "\n         -b yymmddhhmm                  Remove leap second at end of "
      "minute specified");
  printf(
      "\n         -c seconds_to_send             Number of seconds to send "
      "(default 0 = forever)");
  printf(
      "\n         -d                             Start with IEEE 1344 DST "
      "active");
  printf("\n         -D milliseconds                Latency through the codec");
  printf(
      "\n         -f format_type                 i = Modulated IRIG-B 1998 (no "
      "year coded)");
  printf(
      "\n                                        2 = Modulated IRIG-B 2002 "
      "(year coded)");
  printf(
      "\n                                        3 = Modulated IRIG-B w/IEEE "
      "1344 (year & control funcs) (default)");
  printf("\n                                        w = WWV(H)");
  printf(
      "\n         -g yymmddhhmm                  Switch into/out of DST at "
      "beginning of minute specified");
  printf(
      "\n         -i yymmddhhmm                  Insert leap second at end of "
      "minute specified");
  printf(
      "\n         -j                             Disable time rate correction "
      "against system clock (default enabled)");
  printf(
      "\n         -k nn                          Force rate correction for "
      "testing (+1 = add cycle, -1 = remove cycle)");
  printf(
      "\n         -l time_offset                 Set offset of time sent to "
      "UTC as per computer, +/- float hours");
  printf(
      "\n         -o time_offset                 Set IEEE 1344 time offset, "
      "+/-, to 0.5 hour (default 0)");
  printf(
      "\n         -q quality_code_hex            Set IEEE 1344 quality code "
      "(default 0)");
  printf("\n         -r rate                        Set sample rate (Hz)");
  printf(
      "\n         -s                             Set leap warning bit (WWV[H] "
      "only)");
  printf(
      "\n         -t sync_frequency              WWV(H) on-time pulse tone "
      "frequency (default 1200)");
  printf(
      "\n         -u DUT1_offset                 Set WWV(H) DUT1 offset -7 to "
      "+7 (default 0)");
  printf(
      "\n         -x                             Turn off verbose output "
      "(default on)");
  printf(
      "\n         -y yymmddhhmmss                Set initial date and time as "
      "specified (default system time)");
  printf(
      "\n\nThis software licenced under the GPL, modifications performed 2006 "
      "& 2007 by Dean Weiten");
  printf(
      "\nContact: Dean Weiten, Norscan Instruments Ltd., Winnipeg, MB, Canada, "
      "ph (204)-233-9138, E-mail dmw@norscan.com");
  printf("\n\n");
}

/* Reverse string order for nicer print. */
void ReverseString(char *str) {
  int StringLength;
  int IndexCounter;
  int CentreOfString;
  char TemporaryCharacter;

  StringLength = strlen(str);
  CentreOfString = (StringLength / 2) + 1;
  for (IndexCounter = StringLength; IndexCounter >= CentreOfString; IndexCounter--) {
    TemporaryCharacter = str[IndexCounter - 1];
    str[IndexCounter - 1] = str[StringLength - IndexCounter];
    str[StringLength - IndexCounter] = TemporaryCharacter;
  }
}

size_t strlcat(char *dst, const char *src, size_t size) {
  char *d = dst;
  const char *s = src;
  size_t n = size;
  size_t dlen;

  while (n-- != 0 && *d != '\0')
    d++;
  dlen = d - dst;
  n = size - dlen;

  if (n == 0)
    return dlen + strlen(s);
  while (*s != '\0') {
    if (n != 1) {
      *d++ = *s;
      n--;
    }
    s++;
  }
  *d = '\0';

  return dlen + (s - src);
}
