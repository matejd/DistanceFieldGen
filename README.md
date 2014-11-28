Distance Fields
===============

Distance fields seem to be back in vogue. This tiny project features
a distance field generator - it converts an input polygonal mesh into a 3D texture
of closest distance values.
Uses Assimp and CGAL [1]. Also includes a GLFW example (raymarching).

![Screenshot](http://matejd.github.io/DistanceFieldGen/distance-fields-armadillo-screenshot.png)


Build
-----

Uses cmake. Build with something like
```
mkdir -p build/debug/
cd build/debug/
cmake -G 'Unix Makefiles' ../../
make
```

To build the example with Emscripten (for the Web)
```
make release -f EmscriptenMakefile
```
See the makefile for more details.


Dependencies
------------

- Assimp and CGAL
- glfw, glew (example only)


References and resources
------------------------

[1] http://doc.cgal.org/latest/AABB_tree/index.html#Chapter_3D_Fast_Intersection_and_Distance_Computation
