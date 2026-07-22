#ifndef CAMERA_H
#define CAMERA_H
//==============================================================================================
// Originally written in 2016 by Peter Shirley <ptrshrl@gmail.com>
//
// To the extent possible under law, the author(s) have dedicated all copyright and related and
// neighboring rights to this software to the public domain worldwide. This software is
// distributed without any warranty.
//
// You should have received a copy (see file COPYING.txt) of the CC0 Public Domain Dedication
// along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
//==============================================================================================

#include "hittable.h"
#include "material.h"

#ifdef JOLT
#include "jolt/jolt_vm.h"
#include <cstring>

/* Direct-to-memory writer: bypasses ostream, streambuf, and std::string
 * entirely. Writes PPM output straight to JOLT_VM_OUTPUT_START. */
struct jolt_writer {
    uint8_t* ptr;

    jolt_writer() : ptr(reinterpret_cast<uint8_t*>(JOLT_VM_OUTPUT_START)) {}

    void write(const char* data, uint32_t len) {
        std::memcpy(ptr, data, len);
        ptr += len;
    }

    void write_char(char c) { *ptr++ = c; }

    void write_int(int v) {
        if (v >= 100) {
            *ptr++ = '0' + v / 100;
            *ptr++ = '0' + (v / 10) % 10;
            *ptr++ = '0' + v % 10;
        } else if (v >= 10) {
            *ptr++ = '0' + v / 10;
            *ptr++ = '0' + v % 10;
        } else {
            *ptr++ = '0' + v;
        }
    }

    uint32_t size() const {
        return static_cast<uint32_t>(ptr - reinterpret_cast<uint8_t*>(JOLT_VM_OUTPUT_START));
    }

    void flush() {}
};

inline jolt_writer jout;
#define OUT jout

/* JOLT-specific write_color: writes directly to the raw buffer,
 * bypassing ostream/streambuf/string entirely. */
inline void write_color(jolt_writer& out, const color& pixel_color) {
    auto r = pixel_color.x();
    auto g = pixel_color.y();
    auto b = pixel_color.z();

    r = linear_to_gamma(r);
    g = linear_to_gamma(g);
    b = linear_to_gamma(b);

    static const interval intensity(REAL_C(0.000), REAL_C(0.999));
    int rbyte = int(256 * intensity.clamp(r));
    int gbyte = int(256 * intensity.clamp(g));
    int bbyte = int(256 * intensity.clamp(b));

    out.write_int(rbyte); out.write_char(' ');
    out.write_int(gbyte); out.write_char(' ');
    out.write_int(bbyte); out.write_char('\n');
}
#else
#include <iostream>
#define OUT std::cout
#endif


class camera {
  public:
    double aspect_ratio      = 1.0;  // Ratio of image width over height
    int    image_width       = 100;  // Rendered image width in pixel count
    int    samples_per_pixel = 10;   // Count of random samples for each pixel
    int    max_depth         = 10;   // Maximum number of ray bounces into scene

    double vfov     = 90;              // Vertical view angle (field of view)
    point3 lookfrom = point3(0,0,0);   // Point camera is looking from
    point3 lookat   = point3(0,0,-1);  // Point camera is looking at
    vec3   vup      = vec3(0,1,0);     // Camera-relative "up" direction

    double defocus_angle = 0;  // Variation angle of rays through each pixel
    double focus_dist = 10;    // Distance from camera lookfrom point to plane of perfect focus

    void render(const hittable& world) {
        initialize();

        // PPM header: "P3\n<width> <height>\n255\n"
        OUT.write_char('P'); OUT.write_char('3'); OUT.write_char('\n');
        OUT.write_int(image_width); OUT.write_char(' ');
        OUT.write_int(image_height); OUT.write_char('\n');
        OUT.write_char('2'); OUT.write_char('5'); OUT.write_char('5'); OUT.write_char('\n');

        for (int j = 0; j < image_height; j++) {
#ifndef NO_DEBUG_INFO
            std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
#endif
            for (int i = 0; i < image_width; i++) {
                color pixel_color(0,0,0);
                for (int sample = 0; sample < samples_per_pixel; sample++) {
                    ray r = get_ray(i, j);
                    pixel_color += ray_color(r, max_depth, world);
                }
                write_color(OUT, pixel_samples_scale * pixel_color);
            }
        }

#ifndef NO_DEBUG_INFO
        std::clog << "\rDone.                 \n";
#endif
        OUT.flush();
    }

  private:
    int    image_height;         // Rendered image height
    double pixel_samples_scale;  // Color scale factor for a sum of pixel samples
    point3 center;               // Camera center
    point3 pixel00_loc;          // Location of pixel 0, 0
    vec3   pixel_delta_u;        // Offset to pixel to the right
    vec3   pixel_delta_v;        // Offset to pixel below
    vec3   u, v, w;              // Camera frame basis vectors
    vec3   defocus_disk_u;       // Defocus disk horizontal radius
    vec3   defocus_disk_v;       // Defocus disk vertical radius

    void initialize() {
        image_height = int(image_width / aspect_ratio);
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = REAL_C(1.0) / samples_per_pixel;

        center = lookfrom;

        // Determine viewport dimensions.
        auto theta = degrees_to_radians(vfov);
        auto h = std::tan(theta/2);
        auto viewport_height = 2 * h * focus_dist;
        auto viewport_width = viewport_height * (double(image_width)/image_height);

        // Calculate the u,v,w unit basis vectors for the camera coordinate frame.
        w = unit_vector(lookfrom - lookat);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        // Calculate the vectors across the horizontal and down the vertical viewport edges.
        vec3 viewport_u = viewport_width * u;    // Vector across viewport horizontal edge
        vec3 viewport_v = viewport_height * -v;  // Vector down viewport vertical edge

        // Calculate the horizontal and vertical delta vectors from pixel to pixel.
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        // Calculate the location of the upper left pixel.
        auto viewport_upper_left = center - (focus_dist * w) - viewport_u/2 - viewport_v/2;
        pixel00_loc = viewport_upper_left + REAL_C(0.5) * (pixel_delta_u + pixel_delta_v);

        // Calculate the camera defocus disk basis vectors.
        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    ray get_ray(int i, int j) const {
        // Construct a camera ray originating from the defocus disk and directed at a randomly
        // sampled point around the pixel location i, j.

        auto offset = sample_square();
        auto pixel_sample = pixel00_loc
                          + ((i + offset.x()) * pixel_delta_u)
                          + ((j + offset.y()) * pixel_delta_v);

        auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin;

        return ray(ray_origin, ray_direction);
    }

    vec3 sample_square() const {
        // Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
#ifdef RT_CENTER_SAMPLE
        return vec3(0, 0, 0);
#else
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
#endif
    }

    vec3 sample_disk(double radius) const {
        // Returns a random point in the unit (radius 0.5) disk centered at the origin.
        return radius * random_in_unit_disk();
    }

    point3 defocus_disk_sample() const {
        // Returns a random point in the camera defocus disk.
        auto p = random_in_unit_disk();
        return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }

    color ray_color(const ray& r, int depth, const hittable& world) const {
        // If we've exceeded the ray bounce limit, no more light is gathered.
        if (depth <= 0)
            return color(0,0,0);

        hit_record rec;

        if (world.hit(r, interval(REAL_C(0.001), infinity), rec)) {
            ray scattered;
            color attenuation(vec3::uninitialized{});
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color(scattered, depth-1, world);
            return color(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = REAL_C(0.5)*(unit_direction.y() + REAL_C(1.0));
        return (REAL_C(1.0)-a)*color(1.0, 1.0, 1.0) + a*color(0.5, 0.7, 1.0);
    }
};


#endif
