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

#include "rtweekend.h"

#include "camera.h"
#include "hittable.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"


int main() {
    hittable_list world;

    auto ground_material = make_shared<lambertian>(color(REAL_C(0.5), REAL_C(0.5), REAL_C(0.5)));
    world.add(make_shared<sphere>(point3(0,-1000,0), REAL_C(1000.0), ground_material));

#ifndef RT_SMALL_SCENE
    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = random_double();
            point3 center(a + REAL_C(0.9)*random_double(), REAL_C(0.2), b + REAL_C(0.9)*random_double());

            if ((center - point3(4, REAL_C(0.2), 0)).length() > REAL_C(0.9)) {
                shared_ptr<material> sphere_material;

                if (choose_mat < REAL_C(0.8)) {
                    // diffuse
                    auto albedo = color::random() * color::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, REAL_C(0.2), sphere_material));
                } else if (choose_mat < REAL_C(0.95)) {
                    // metal
                    auto albedo = color::random(REAL_C(0.5), 1);
                    auto fuzz = random_double(0, REAL_C(0.5));
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, REAL_C(0.2), sphere_material));
                } else {
                    // glass
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }
#endif

    auto material1 = make_shared<dielectric>(REAL_C(1.5));
    world.add(make_shared<sphere>(point3(0, 1, 0), REAL_C(1.0), material1));

    auto material2 = make_shared<lambertian>(color(REAL_C(0.4), REAL_C(0.2), REAL_C(0.1)));
    world.add(make_shared<sphere>(point3(-4, 1, 0), REAL_C(1.0), material2));

    auto material3 = make_shared<metal>(color(REAL_C(0.7), REAL_C(0.6), REAL_C(0.5)), REAL_C(0.0));
    world.add(make_shared<sphere>(point3(4, 1, 0), REAL_C(1.0), material3));

    camera cam;

#ifndef RT_IMAGE_WIDTH
#define RT_IMAGE_WIDTH 1200
#endif
#ifndef RT_SAMPLES
#define RT_SAMPLES 10
#endif
#ifndef RT_DEPTH
#define RT_DEPTH 20
#endif
    cam.aspect_ratio      = REAL_C(16.0) / REAL_C(9.0);
    cam.image_width       = RT_IMAGE_WIDTH;
    cam.samples_per_pixel = RT_SAMPLES;
    cam.max_depth         = RT_DEPTH;

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = REAL_C(0.6);
    cam.focus_dist    = REAL_C(10.0);

    cam.render(world);
}
