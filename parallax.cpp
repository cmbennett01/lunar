#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "watdefs.h"
#include "afuncs.h"
#include "mpc_func.h"
#include "stringex.h"

/* Test program I run when I encounter a latitude/altitude and want to turn
it into parallax constants,  or vice versa.  See following 'error_exit'
function,  or run without command line arguments,  for usage.

   When compiled with CGI_VERSION #defined,  one gets a version suitable
for on-line use;  see https://www.projectpluto.com/parallax.htm for
an example of its usage.   */

#define EARTH_MAJOR_AXIS_IN_METERS    6378137.
     /* This code currently uses the GRS1980 value for the minor axis.
        That's about 0.105 mm less than the WGS1984 value.  I do not
        know of a case where the difference is actually measurable.  */
#define EARTH_MINOR_AXIS_IN_METERS    6356752.314140347
#define PI 3.1415926535897932384626433832795028841971693993751058209749445923

typedef struct
{
   double lat, lon, alt;
   double rho_sin_phi, rho_cos_phi, x, y;
} loc_t;

static char *show_angle( char *buff, double angle)
{
   const long microarcsec = (long)( fabs( angle) * 3600e+5);

   *buff = (angle < 0 ? '-' : '+');
   snprintf_err( buff + 1, 16, "%02ld %02ld %02ld.%05ld",
               (microarcsec / 360000000L),         /* degrees */
               (microarcsec / 6000000L) % 60L,     /* arcminutes */
               (microarcsec / 100000L) % 60L,     /* arcseconds */
                microarcsec % 100000L);           /* milliarcseconds */
   return( buff);
}

static int get_mpc_obscode_data( loc_t *loc, const char *mpc_code)
{
   int pass, rval = -1;
   char end_char = mpc_code[3];

   if( !end_char)
      end_char = ' ';
   for( pass = 0; pass < 2 && rval; pass++)
      {
      const char *ifilename = (pass ? "ObsCodes.htm" : "rovers.txt");
      FILE *ifile;
      char buff[200];
      mpc_code_t code_data;

#ifdef CGI_VERSION
      strlcpy_error( buff, "/home/projectp/public_html/cgi_bin/fo/");
#else
      strlcpy_error( buff, getenv( "HOME"));
      strlcat_error( buff, "/.find_orb/");
#endif
      strlcat_error( buff, ifilename);
      ifile = fopen( buff, "rb");
      if( !ifile)
         ifile = fopen( ifilename, "rb");
      if( !ifile)
         perror( buff);
      assert( ifile);
      while( rval && fgets( buff, sizeof( buff), ifile))
         if( !memcmp( buff, mpc_code, 3) && buff[3] == end_char)
            {
            const int err_code = get_mpc_code_info( &code_data, buff);

            if( 3 == err_code)
               {
               loc->lat = code_data.lat;
               loc->lon = code_data.lon;
               if( loc->lon > PI)
                  loc->lon -= PI + PI;
               loc->alt = code_data.alt;
               loc->rho_sin_phi = code_data.rho_sin_phi;
               loc->rho_cos_phi = code_data.rho_cos_phi;
               loc->x = cos( loc->lon) * loc->rho_cos_phi;
               loc->y = sin( loc->lon) * loc->rho_cos_phi;
               printf( "%s !%+014.9f  %+013.9f %9.3f   %s\n", mpc_code,
                     loc->lon * 180. / PI,
                     loc->lat * 180. / PI,
                     loc->alt, code_data.name);
               }
            else if( -1 != err_code)
               printf( "%s\n", code_data.name);
            rval = 0;
            }
      fclose( ifile);
      }
   if( rval)
      {
      printf( "Couldn't find observatory code (%s)\n", mpc_code);
      exit( -1);
      }
   return( rval);
}

static void show_location( const loc_t *loc)
{
   char buff[80];

   if( loc->lon)
      {
      printf( "Longitude %14.9f = %s\n", loc->lon, show_angle( buff, loc->lon));
      if( loc->lon < 0.)
         printf( "Longitude %14.9f = %s\n", loc->lon + 360., show_angle( buff, loc->lon + 360.));
      if( loc->lon > 180.)
         printf( "Longitude %14.9f = %s\n", loc->lon - 360., show_angle( buff, loc->lon - 360.));
      }
   printf( "Latitude  %11.9f = %s\n", loc->lat, show_angle( buff, loc->lat));
   printf( "Altitude %.5f meters\n", loc->alt);
   printf( "Parallax constants %.11f %+.11f\n", loc->rho_cos_phi, loc->rho_sin_phi);
   printf( "In meters: %.5f %+.5f\n", loc->rho_cos_phi * EARTH_MAJOR_AXIS_IN_METERS,
                                      loc->rho_sin_phi * EARTH_MAJOR_AXIS_IN_METERS);
   if( loc->lon)
      {
      FILE *ifile = fopen( "geo_rect.txt", "rb");

#ifndef CGI_VERSION
      if( !ifile)
         {
         strlcpy_error( buff, getenv( "HOME"));
         strlcat_error( buff, "/.find_orb/geo_rect.txt");
         ifile = fopen( buff, "rb");
         }
#endif
      printf( "xyz in Earth radii %+.7f %+.7f %+.7f\n",
                  loc->x, loc->y, loc->rho_sin_phi);
      printf( "xyz in meters      %+.5f %+.5f %+.5f\n",
                                      loc->x * EARTH_MAJOR_AXIS_IN_METERS,
                                      loc->y * EARTH_MAJOR_AXIS_IN_METERS,
                                      loc->rho_sin_phi * EARTH_MAJOR_AXIS_IN_METERS);
      if( ifile)
         {
         extract_region_data_for_lat_lon( ifile, buff, loc->lat, loc->lon);
         if( *buff)
            printf( "This point is somewhere in %s\n", buff + 2);
         fclose( ifile);
         }
      }
}

#ifndef CGI_VERSION

static void set_location_two_params( loc_t *loc, double rho_cos_phi,
                                                 double rho_sin_phi)
{
   if( fabs( rho_cos_phi) > 2. || fabs( rho_sin_phi) > 2.)
      {             /* looks like parallax values in meters */
      rho_cos_phi /= EARTH_MAJOR_AXIS_IN_METERS;
      rho_sin_phi /= EARTH_MAJOR_AXIS_IN_METERS;
      }
   loc->rho_cos_phi = rho_cos_phi;
   loc->rho_sin_phi = rho_sin_phi;
   loc->lat = point_to_ellipse( 1., EARTH_MINOR_AXIS_IN_METERS / EARTH_MAJOR_AXIS_IN_METERS,
                 rho_cos_phi, rho_sin_phi, &loc->alt);
   loc->alt *= EARTH_MAJOR_AXIS_IN_METERS;
   loc->x = rho_cos_phi;
   loc->lon = loc->y = 0.;
}

static void set_location_three_params( loc_t *loc, double p1, double p2, double p3)
{
   if( fabs( p1) > 400 || fabs( p2) > 400)
      {             /* looks like parallax values in meters */
      p1 /= EARTH_MAJOR_AXIS_IN_METERS;
      p2 /= EARTH_MAJOR_AXIS_IN_METERS;
      p3 /= EARTH_MAJOR_AXIS_IN_METERS;
      }
   if( fabs( p1) < 2. && fabs( p2) < 2.)
      {
      const double rho_cos_phi = sqrt( p1 * p1 + p2 * p2);

      set_location_two_params( loc, rho_cos_phi, p3);
      loc->x = p1;
      loc->y = p2;
      loc->lon = atan2( p2, p1);
      }
   else        /* p1 = longitude, p2 = latitude, p3 = alt in meters */
      {
      loc->lon = p1 * PI / 180.;
      loc->lat = p2 * PI / 180.;
      loc->alt = p3;
      lat_alt_to_parallax( loc->lat, loc->alt, &loc->rho_cos_phi, &loc->rho_sin_phi,
               EARTH_MAJOR_AXIS_IN_METERS, EARTH_MINOR_AXIS_IN_METERS);
      loc->x = loc->rho_cos_phi * cos( loc->lon);
      loc->y = loc->rho_cos_phi * sin( loc->lon);
      }
}

static void error_exit( void)
{
   printf( "Run 'parallax' with three arguments (latitude, longitude, altitude)\n"
           "and they will be converted to parallax constants.  Run with two arguments\n"
           "(rho_cos_phi, rho_sin_phi) and the corresponding latitude and altitude\n"
           "will be computed and shown.  Run with one argument (MPC obscode) and\n"
           "all the location data for it wiss be shown.\n");
   exit( -1);
}

int main( const int argc, const char **argv)
{
   loc_t loc;

   if( argc < 2 || argc > 4)
      error_exit( );
   else if( argc == 2)          /* MPC code provided */
      get_mpc_obscode_data( &loc, argv[1]);
   else if( argc == 3 && 3 == strlen( argv[1]) && 3 == strlen( argv[2]))
      {           /* two MPC codes provided */
      loc_t loc2;
      double dist, posn_ang, p1[2], p2[2];

      get_mpc_obscode_data( &loc, argv[1]);
      get_mpc_obscode_data( &loc2, argv[2]);
      p1[0] = loc.lon;
      p1[1] = loc.lat;
      p2[0] = loc2.lon;
      p2[1] = loc2.lat;
      calc_dist_and_posn_ang( p1, p2, &dist, &posn_ang);
      printf( "(%s) is %.3f km from (%s),  at bearing %.2f (0=N, 90=E, 180=S, 270=W)\n",
               argv[2], dist * EARTH_MAJOR_AXIS_IN_METERS / 1000.,
               argv[1], 360. - posn_ang * 180. / PI);
      }
   else if( argc == 3)          /* parallax constants provided */
      set_location_two_params( &loc, atof( argv[1]), atof( argv[2]));
   else        /* argc == 4 */
      set_location_three_params( &loc, atof( argv[1]), atof( argv[2]), atof( argv[3]));
   loc.lat *= 180. / PI;
   loc.lon *= 180. / PI;
   show_location( &loc);
}
#else

#ifdef __has_include
   #if __has_include("cgi_func.h")
       #include "cgi_func.h"
   #else
       #error   \
         'cgi_func.h' not found.  This project depends on the 'lunar'\
         library.  See www.github.com/Bill-Gray/lunar .\
         Clone that repository,  'make'  and 'make install' it.
       #ifdef __GNUC__
         #include <stop_compiling_here>
            /* Above line suppresses cascading errors. */
       #endif
   #endif
#else
   #include "cgi_func.h"
#endif
#include <string.h>

static double get_angle( const char *ibuff)
{
   bool is_negative = false;
   double rval;
   char field1[40], field2[40], field3[40];

   if( *ibuff == '-')
      {
      ibuff++;
      is_negative = true;
      }
   else if( *ibuff == '+')
      ibuff++;
   *field1 = *field2 = *field3 = '\0';
   sscanf( ibuff, "%20s %20s %20s", field1, field2, field3);
   rval = atof( field1) + atof( field2) / 60. + atof( field3) / 3600.;
   if( is_negative)
      rval = -rval;
   return( rval);
}

int main( void)
{
   loc_t loc;
   char field[30], buff[100];
   int rval;
   double xyz[3];

   printf( "Content-type: text/html\n\n");
   printf( "<pre>");
   avoid_runaway_process( 300);
   rval = initialize_cgi_reading( );
   if( rval <= 0)
      {
      printf( "<p> <b> CGI data reading failed : error %d </b>", rval);
      printf( "This isn't supposed to happen.</p>\n");
      return( 0);
      }
   memset( &loc, 0, sizeof( loc_t));
   xyz[0] = xyz[1] = xyz[2] = 0.;
   while( !get_cgi_data( field, buff, NULL, sizeof( buff)))
      {
      if( !strcmp( field, "rho_sin_phi"))
         loc.rho_sin_phi = atof( buff);
      else if( !strcmp( field, "rho_cos_phi"))
         loc.rho_cos_phi = atof( buff);
      else if( !strcmp( field, "lat") && strchr( buff, '.'))
         loc.lat = get_angle( buff) * PI / 180.;
      else if( !strcmp( field, "lon") && strchr( buff, '.'))
         loc.lon = get_angle( buff) * PI / 180.;
      else if( !strcmp( field, "alt"))
         loc.alt = atof( buff) / EARTH_MAJOR_AXIS_IN_METERS;
      else if( !memcmp( field, "xyz", 3) && field[3] >= '0' && field[3] <= '2')
         xyz[field[3] - '0'] = atof( buff);
      else if( !strcmp( field, "mpc_code"))
         get_mpc_obscode_data( &loc, buff);
      }
   if( xyz[0] || xyz[1] || xyz[2])
      {
      loc.lon = atan2( xyz[1], xyz[0]);
      loc.rho_cos_phi = sqrt( xyz[0] * xyz[0] + xyz[1] * xyz[1]);
      loc.rho_sin_phi = xyz[2];
      }
   if( loc.rho_sin_phi && loc.rho_cos_phi)
      {
      if( fabs( loc.rho_sin_phi) > 100. || fabs( loc.rho_cos_phi) > 100.)
         {           /* assume meters were entered */
         loc.rho_sin_phi /= EARTH_MAJOR_AXIS_IN_METERS;
         loc.rho_cos_phi /= EARTH_MAJOR_AXIS_IN_METERS;
         }
      loc.lat = point_to_ellipse( 1., EARTH_MINOR_AXIS_IN_METERS / EARTH_MAJOR_AXIS_IN_METERS,
                 loc.rho_cos_phi, loc.rho_sin_phi, &loc.alt);
      }
   else if( loc.lat)
      lat_alt_to_parallax( loc.lat, loc.alt, &loc.rho_cos_phi, &loc.rho_sin_phi,
              1., EARTH_MINOR_AXIS_IN_METERS / EARTH_MAJOR_AXIS_IN_METERS);
   else
      {
      printf( "Must provide either rho sin(phi) and rho cos(phi),  in\n"
              "which case the latitude and altitude will be computed and\n"
              "shown;  <i>or</i> latitude and altitude,  in which case you'll\n"
              "get the parallax constants as output.  Or,  you can provide\n"
              "x, y, and z;  or an MPC code.  Hit the Back-arrow in your\n"
              "browser and review your options.\n");
      return( 0);
      }
   loc.x = loc.rho_cos_phi * cos( loc.lon);
   loc.y = loc.rho_cos_phi * sin( loc.lon);
   loc.lat *= 180. / PI;
   loc.lon *= 180. / PI;
   loc.alt *= EARTH_MAJOR_AXIS_IN_METERS;
   show_location( &loc);
   if( loc.lon)
      {
      if( loc.lon > 180.)
         loc.lon -= 360.;
      printf( "<a href='http://maps.google.com/maps?q=%.7f,%.7f'>",
                        loc.lat, loc.lon);
      printf( "Click here for a G__gle map of this location</a>\n");
      printf( "<a href='https://www.bing.com/maps/?cp=%.7f~%.7f&lvl=18&style=a'>",
                        loc.lat, loc.lon);
      printf( "Click here for a Bing map of this location</a>\n");
      }
   return( 0);
}
#endif
