/* Code to add satellite offset data to 80-column MPC formatted
astrometry,  using coordinates downloaded from JPL Horizons.  Can be
compiled both as a standalone utility and as the code behind an
on-line one;  see https://www.projectpluto.com/add_off.htm .
For documentation of how satellite offsets are formatted,  see

https://minorplanetcenter.net/iau/info/SatelliteObs.html

   and some of the comments below.  Note that SOHO data,  at least,
is also available at

https://sohowww.nascom.nasa.gov/data/ancillary/orbit/

   Haven't checked that source out carefully,  since Horizons
has worked well thus far,  but the above URL could come in handy. */

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "watdefs.h"
#include "afuncs.h"
#include "mpc_func.h"
#include "stringex.h"

typedef struct
{
   double jd, xyz[3], vel[3];
   char mpc_code[4];
} offset_t;

const double tolerance = 1e-5;
int verbose = 0;
int n_positions_set = 0, n_positions_failed = 0;

/* If the observation is from a spacecraft,  return the JDE of the
observation.  (Horizons expects times for vector ephems in JDE,  not UTC
JDs.)  We expect the time to be,  at minimum,  after HST was launched. */

static double get_sat_obs_jd( const char *buff)
{
   const double hst_launch_jd = 2448005.5;  /* 1990 April 24 */
   double jd = 0.;

   if( strlen( buff) > 80 && (buff[14] == 'S' || buff[14] == 's'))
      jd = extract_date_from_mpc_report( buff, NULL);
   if( jd < hst_launch_jd)
      jd = 0.;
   else
      jd += td_minus_utc( jd) / seconds_per_day;
   return( jd);
}

/* The following conversion table is going to need occasional fixes.
Cas = Cassini,  SoO = Solar Orbiter,  and PSP = Parker Solar Probe
are _not_ official MPC codes.  */

typedef struct
{
   char mpc_code[4];
   int jpl_desig;
} xref_t;

static int get_horizons_idx( const char *mpc_code)
{
   static const xref_t xrefs[] = {
          {"245",     -79 },   /* Spitzer            */
          {"249",     -21 },   /* SOHO               */
          {"250",     -48 },   /* Hubble             */
          {"258", -139479 },   /* Gaia               */
          {"Cas",     -82 },   /* Cassini            */
          {"C49",    -234 },   /* STEREO-A           */
          {"C50",    -235 },   /* STEREO-B           */
          {"C51",    -163 },   /* WISE               */
          {"C52", -128485 },   /* Swift              */
          {"C53", -139089 },   /* NEOSSAT            */
          {"C54",     -98 },   /* New Horizons       */
          {"C55",    -227 },   /* Kepler             */
          {"C56", -141043 },   /* LISA Pathfinder    */
          {"C57",     -95 },   /* TESS               */
          {"C59", -148840 },   /* Yangwang-1         */
          {"PSP",     -96 },   /* Parker Solar Probe */
          {"274",    -170 },   /* James Webb (Space) Telescope */
          {"SoO",    -144 }};  /* Solar Orbiter      */
   size_t i;
   const size_t n_xrefs = sizeof( xrefs) / sizeof( xrefs[0]);

   for( i = 0; i < n_xrefs; i++)
      if( !memcmp( mpc_code, xrefs[i].mpc_code, 3))
         return( xrefs[i].jpl_desig);
   return( 0);
}

/* The following modifies an 'S' (satellite RA/dec line) into an 's' line
(satellite offset from the center of the earth).

   The signs of the x, y, z offsets are stored in columns 35, 47,  and 59.
|x| is stored in columns 36-45, |y| in 48-57, |z| in 60-69.

   If the greatest offset is less than ten million km,  the offsets are stored
in units of km,  and column 33 contains a '1'.  Offsets under 100000 km are
stored with the decimal point in column 41, 53,  or 65.  Those up to a million
are stored with decimal points in columns 42, 54,  or 66;  those up to ten
million shift the decimal point an additional column.  There will be a space
between the sign and the absolute value for offsets under 10000 km.

   If the greatest offset is over ten million km,  the offsets are stored
in AUs,  and column 33 contains a '2'.  (MPC sometimes uses this scheme
for smaller offsets as well.)  Offsets over 10 AUs are stored with the
decimal point in columns 38,  50,  or 52 (this happens for _New Horizons_
observations).  Smaller offsets have the decimal point in columns 37, 49,
or 51.  This handles any offset up to 100 AU.

   Examples of the possible formats :

     LTMQ6Ga  s2019 06 26.2809121 -66851.9880 +403817.120 + 9373.8070   NEOCPC57
     K20K42H  s2020 12 25.5287142 +14.3956075 -44.6290151 -17.5105651   ~5zHCC54
    CK10Y100 Gs2010 12 18.42987 2 -1.01982175 -0.76936943 -0.33509167   84456C49
z9987K06UJ8Y  s2019 07 26.2427421 + 551363.13 -1190783.85 - 650915.72   ~3GcZ258
*/

static int set_mpc_style_offsets( char *buff, const double *xyz)
{
   bool output_in_au = false;
   int i;

   for( i = 0; i < 3; i++)
      if( xyz[i] > 9999999.0 || xyz[i] < -9999999.0)
         output_in_au = true;
   memset( buff + 33, ' ', 39);
   buff[32] = (output_in_au ? '2' : '1');
   buff[33] = ' ';
   for( i = 0; i < 3; i++)
      {
      char *optr = buff + 34 + i * 12;
      const char *format;
      double oval = fabs( xyz[i]);

      *optr++ = (xyz[i] > 0. ? '+' : '-');
      if( output_in_au)
         {
         oval /= AU_IN_KM;
         format = (oval > 9.9 ? "%10.7f " : "%10.8f ");
         }
      else           /* output in kilometers */
         {
         if( oval > 999999.)
            format = "%10.2f ";
         else if( oval > 99999.)
            format = "%10.3f ";
         else
            format = "%10.4f ";
         }
      snprintf_err( optr, 12, format, oval);
      }
   buff[14] = 's';
   buff[70] = ' ';
   return( 0);
}

const char *cmd_start = "curl -s -o /tmp/locs %s"
    "\"https://ssd.jpl.nasa.gov/horizons_batch.cgi?batch=1&COMMAND='%d'"
    "&REF_PLANE='FRAME'"
    "&OBJ_DATA='NO'&TABLE_TYPE='V'&TLIST=";

const char *cmd_end = "&VEC_TABLE='2'&VEC_LABELS='N'\"";

/* To set the time offsets,  we send a query to JPL Horizons that will
look something like the following (split here over four lines for ease
of explanation) :

https://ssd.jpl.nasa.gov/horizons_batch.cgi?batch=1&COMMAND='-163'
&REF_PLANE='FRAME'&OBJ_DATA='NO'&TABLE_TYPE='V'&TLIST=
'2458843.421181','2458843.486631','2458843.551951','2458843.616891',
&VEC_TABLE='2'&VEC_LABELS='N'

This requests positions on the four JDEs given on the third line for
object -163 (which is Horizons' index for (C51) WISE.)  REF_PLANE='FRAME'
specifies J2000 equatorial coordinates.  TABLE_TYPE='V' specifies
vectors.  VEC_TABLE='2' specifies positions and velocities.

   Each time adds 17 bytes to our URL.  I can send JPL an 8000-byte
URL,  but not much beyond that without getting errors.  After
allowing for the header and trailer data,  we can request 458
offsets without overflowing the 8000-byte URL.   So if we
encounter an unset offset,  we look for up to 457 other instances
where that particular obscode was used,  form a query to ask for
all of them, and then set up to ask for up to 458 offsets at a go.  */

static int set_offsets( offset_t *offsets, const int n_offsets)
{
   char buff[8000];     /* supports 458 offsets at a go */
   int i;
   const int horizons_idx = get_horizons_idx( offsets->mpc_code);

   if( !horizons_idx)
      {
      printf( "ERROR! MPC code '%s' wasn't found.\n", offsets->mpc_code);
      printf( "Either it's not an MPC code,  or it's not one of the spacecraft\n");
      printf( "that this software knows about.  Check the 'add_off.c' source\n");
      printf( "code,  and/or contact the author.\n");
      for( i = 0; i < n_offsets; i++)
         if( !strcmp( offsets[i].mpc_code, offsets[0].mpc_code))
            offsets[i].xyz[0] = -0.1;     /* mark as "don't try again" */
      return( 0);
      }
   snprintf_err( buff, sizeof( buff), cmd_start,
            (verbose ? "" : "-q "), horizons_idx);
   for( i = 0; i < n_offsets; i++)
      if( !strcmp( offsets[i].mpc_code, offsets[0].mpc_code))
         {
         if( i)
            strlcat_err( buff, ",", sizeof( buff));
         snprintf_err( buff + strlen( buff), 18, "'%.6f'", offsets[i].jd);
         if( strlen( buff) + 60 > sizeof( buff))
            break;
         }
   strlcat_err( buff, cmd_end, sizeof( buff));
   if( verbose)
      printf( "%s\n", buff);
   if( !system( buff))
      {
      FILE *ifile = fopen( "/tmp/locs", "rb");

      assert( ifile);
      while( fgets( buff, sizeof( buff), ifile))
         if( strstr( buff, " = A.D. ") && strstr( buff, " TDB"))
            {
            const double jd = atof( buff);
            double state[6];
            int j;

            if( verbose)
               printf( "Found locations\n%s", buff);
            for( j = 0; j < 6; j += 3)
               {
               int n_found;

               if( !fgets( buff, sizeof( buff), ifile))
                  assert( 0);
               n_found = sscanf( buff, "%lf %lf %lf", state + j,
                                 state + j + 1, state + j + 2);
               assert( 3 == n_found);
               }
            for( j = 0; j < i; j++)
               if( !strcmp( offsets[j].mpc_code, offsets[0].mpc_code)
                        && fabs( offsets[j].jd - jd) < tolerance
                        && !offsets[j].xyz[0] && !offsets[j].xyz[1]
                        && !offsets[j].xyz[2])
                  {
                  offsets[j].xyz[0] = state[0];
                  offsets[j].xyz[1] = state[1];
                  offsets[j].xyz[2] = state[2];
                  offsets[j].vel[0] = state[3];
                  offsets[j].vel[1] = state[4];
                  offsets[j].vel[2] = state[5];
                  n_positions_set++;
                  }
            }
         else if( verbose > 1 || !memcmp( buff, "No ephemeris", 12))
            printf( "%s", buff);
      fclose( ifile);
      }
   else
      {
      printf( "Error with system() : '%s'\n", strerror( errno));
      printf( "'%s'\n", buff);
      }
         /* If some or all obs weren't set,  zero their MPC codes.  That */
         /* will keep us from making repeated failed requests for them.  */
   for( i--; i > 0; i--)
      if( !strcmp( offsets[i].mpc_code, offsets[0].mpc_code))
         if( !offsets[i].xyz[0])
            {
            n_positions_failed++;
            offsets[i].mpc_code[0] = '\0';
            }
   return( 0);
}

/* The following reads the input file and looks for 80-column obs from
spacecraft.  On a second pass,  it removes any existing 's' (spacecraft
position) records and replaces them with 's' records created from
the Horizons ephems.   */

int process_file( const char *filename)
{
   FILE *ifile = fopen( filename, "rb");
   char buff[300];
   double jd;
   time_t t0 = time( NULL);
   offset_t *offsets = NULL;
   int i, n_offsets = 0;

   assert( ifile);
   printf( "COM add_off ver 2022 Dec 07,  run %s", ctime( &t0));
   while( fgets( buff, sizeof( buff), ifile))
      if( buff[14] == 'S' && (jd = get_sat_obs_jd( buff)) != 0.)
         {
         if( verbose)
            printf( "Sat obs: %.5f\n%s", jd, buff);
         n_offsets++;
         offsets = (offset_t *)realloc( offsets,
                              n_offsets * sizeof( offset_t));
         memset( offsets + n_offsets - 1, 0, sizeof( offset_t));
         offsets[n_offsets - 1].jd = jd;
         memcpy( offsets[n_offsets - 1].mpc_code, buff + 77, 3);
         }
   for( i = 0; i < n_offsets; i++)
      {
      if( verbose)
         printf( "%d: JD %.5f; code '%s'\n", i, offsets[i].jd, offsets[i].mpc_code);
      if( !offsets[i].xyz[0] && offsets[i].mpc_code[0])
         set_offsets( offsets + i, n_offsets - i);
      }
   fseek( ifile, 0, SEEK_SET);
   while( fgets( buff, sizeof( buff), ifile))
      if( (jd = get_sat_obs_jd( buff)) <= 0.)    /* not an observation;  */
         printf( "%s", buff);                    /* just pass it through */
      else if( buff[14] != 's')
         {
         int idx = -1;

         for( i = 0; idx < 0 && i < n_offsets; i++)
            if( !memcmp( buff + 77, offsets[i].mpc_code, 3)
                       && fabs( jd - offsets[i].jd) < tolerance)
               idx = i;
         if( idx >= 0)
            {
            char vel_buff[80];

            snprintf_err( vel_buff, sizeof( vel_buff),
                     "COM vel (km/s) %.16s%+13.7f%+13.7f%+13.7f %.3s\n",
                     buff + 15,
                     offsets[idx].vel[0], offsets[idx].vel[1],
                     offsets[idx].vel[2], offsets[idx].mpc_code);
            printf( "%s", vel_buff);
            printf( "%s", buff);
            set_mpc_style_offsets( buff, offsets[idx].xyz);
            printf( "%s", buff);
            }
         else
            printf( "%s", buff);
         }
   fclose( ifile);
   free( offsets);
   printf( "COM %d positions set by add_off; %d failed in %.2f seconds\n",
         n_positions_set, n_positions_failed,
         (double)clock( ) / (double)CLOCKS_PER_SEC);
   return( 0);
}

#ifdef ON_LINE_VERSION
int dummy_main( const int argc, const char **argv)
#else
int main( const int argc, const char **argv)
#endif
{
   int i;

   if( argc < 2)
      {
      fprintf( stderr, "'add_off' takes the name of an input file of astrometry\n"
                       "as a command-line argument.\n");
      return( -1);
      }
   for( i = 2; i < argc; i++)
      if( argv[i][0] == '-')
         switch( argv[i][1])
            {
            case 'v':
               verbose = 1;
               if( atoi( argv[i] + 2))
                  verbose = atoi( argv[i] + 2);
               printf( "Verbose = %d\n", verbose);
               break;
            default:
               printf( "Option '%s' unrecognized\n", argv[i]);
               break;
            }
   return( process_file( argv[1]));
}

#ifdef ON_LINE_VERSION
#include "cgi_func.h"

int main( void)
{
   const char *argv[20];
   const size_t max_buff_size = 40000;
   char *buff = (char *)malloc( max_buff_size);
   char field[30];
   FILE *lock_file = fopen( "lock.txt", "w");
   extern char **environ;
   int cgi_status;
   const char *temp_filename = "/tmp/add_off.txt";
   size_t bytes_written = 0;

   avoid_runaway_process( 15);
   printf( "Content-type: text/html\n\n");
   printf( "<html> <body> <pre>\n");
   if( !lock_file)
      {
      printf( "<p> Server is busy.  Try again in a minute or two. </p>");
      printf( "<p> Your TLEs are very important to us! </p>");
      return( 0);
      }
   setbuf( lock_file, NULL);
   fprintf( lock_file, "'add_off' : We're in\n");
   for( size_t i = 0; environ[i]; i++)
      fprintf( lock_file, "%s\n", environ[i]);
   cgi_status = initialize_cgi_reading( );
   fprintf( lock_file, "CGI status %d\n", cgi_status);
   if( cgi_status <= 0)
      {
      printf( "<p> <b> CGI data reading failed : error %d </b>", cgi_status);
      printf( "This isn't supposed to happen.</p>\n");
      return( 0);
      }
   while( !get_cgi_data( field, buff, NULL, max_buff_size))
      {
      if( !strcmp( field, "TextArea") || !strcmp( field, "upfile"))
         {
         if( strlen( buff) > 70)
            {
            const char *tptr = strstr( buff, "COM verbo");
            int i;
            FILE *ofile = fopen( temp_filename,
                               (bytes_written ? "ab" : "wb"));

            assert( ofile);
            for( i = 1; buff[i + 1]; i++)     /* cvt Mac-style CR endings */
               if( buff[i] == 13 && buff[i + 1] != 10)   /* to LF endings */
                  buff[i] = 10;
            bytes_written += fwrite( buff, 1, strlen( buff), ofile);
            fprintf( ofile, "\n");
            fclose( ofile);
            if( tptr)
               verbose = 1;
            }
         }
      }
   fprintf( lock_file, "%d bytes written\n", (int)bytes_written);
   free( buff);
   argv[0] = "add_off";
   argv[1] = temp_filename;
   argv[2] = NULL;
   dummy_main( 2, argv);
   fprintf( lock_file, "Done\n");
   printf( "</pre> </body> </html>");
   fclose( lock_file);
   return( 0);
}
#endif

/* The following should test all four cases : offsets > 10 AU,
offsets < 10 AU,  offsets > 100000 km,  offsets < 100000 km.

     K20K42H  S2020 12 25.69572814 45 21.50 +04 41 41.2                V~5zHCC54
     K20K42H  s2020 12 25.6957282 +14.3990440 -44.6299726 -17.5109273   ~5zHCC54
     K20K42H  S2020 12 25.77836614 45 13.89 +04 42 58.7          18.3 rV~5zHCC54
     K20K42H  s2020 12 25.7783662 +14.4007441 -44.6304436 -17.5111053   ~5zHCC54
     K20K42H  S2020 12 25.77871414 45 13.86 +04 43 00.8                V~5zHCC54
     K20K42H  s2020 12 25.7787142 +14.4007513 -44.6304455 -17.5111061   ~5zHCC54
     K20K42H  S2020 12 25.77906114 45 13.90 +04 42 59.1                V~5zHCC54
     K20K42H  s2020 12 25.7790612 +14.4007584 -44.6304475 -17.5111068   ~5zHCC54

     LTMQ6Ga  S2019 07 09.15590615 19 40.855-81 39 02.92   ~8I3Y 15.5 GVNEOCPC57
     LTMQ6Ga  s2019 07 09.1559061 +10834.2820 +393453.279 +35824.8090   NEOCPC57
     LTMQ6Ga  S2019 07 09.17674015 19 39.658-81 37 45.91   ~8I3Y 15.4 GVNEOCPC57
     LTMQ6Ga  s2019 07 09.1767401 + 9810.8730 +393911.293 +35488.9210   NEOCPC57
     LTMQ6Ga  S2019 07 09.30173915 19 37.445-81 30 04.97   ~8I3Y 15.9 GVNEOCPC57
     LTMQ6Ga  s2019 07 09.3017391 + 3667.8710 +396488.678 +33459.2930   NEOCPC57

    CK10Y100 GS2010 12 18.42987 00 15 39.65 -05 26 23.0                 84456C49
    CK10Y100 Gs2010 12 18.42987 2 -1.01982175 -0.76936943 -0.33509167   84456C49
    CK10Y100 GS2010 12 18.45765 00 15 23.02 -05 23 25.3                 84456C49
    CK10Y100 Gs2010 12 18.45765 2 -1.01940694 -0.76983731 -0.33529369   84456C49
    CK10Y100 GS2010 12 18.48543 00 15 07.42 -05 22 04.2                 84456C49
    CK10Y100 Gs2010 12 18.48543 2 -1.01899187 -0.77030502 -0.33549562   84456C49

    CK05L030  S2010 05 24.27985 11 45 53.84 +41 53 18.8                w70582C51
    CK05L030  s2010 05 24.27985 1 - 3522.9048 + 2925.0063 + 5163.4745   70582C51
    CK05L030  S2010 05 24.27998 11 45 53.79 +41 53 20.5                w70582C51
    CK05L030  s2010 05 24.27998 1 - 3464.8458 + 2898.3010 + 5217.4255   70582C51
    CK05L030  S2010 05 24.54446 11 45 46.04 +41 52 28.6                w70582C51
    CK05L030  s2010 05 24.54446 1 - 3542.7544 + 2911.7722 + 5157.8181   70582C51

    CK06O040 3S2006 07 20.52922 07 47 36.9  +19 21 41                   57549249
    CK06O040 3s2006 07 20.52922 2 -0.00837351 +0.00591646 +0.00244197   57549249
    CK06O040 3S2006 07 20.57089 07 48 27.3  +19 28 59                   57549249
    CK06O040 3s2006 07 20.57089 2 -0.00837914 +0.00591334 +0.00243939   57549249
    CK06O040 3S2006 07 20.59589 07 48 56.7  +19 33 32                   57549249
    CK06O040 3s2006 07 20.59589 2 -0.00838252 +0.00591147 +0.00243785   57549249
    CK06O040 3S2006 07 20.61255 07 49 22.4  +19 37 38                   57549249
    CK06O040 3s2006 07 20.61255 2 -0.00838477 +0.00591022 +0.00243682   57549249
*/
