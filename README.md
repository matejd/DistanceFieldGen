Distance Fields
===============

Distance fields seem to be in vogue this year. This tiny project features
a distance field generator - it generates a 3D texture from input polygonal mesh.
Uses Assimp and CGAL [1]. Also includes a GLFW example.


Build
-----

Uses cmake. Build with something like
```
mkdir -p build/debug/
cd build/debug/
cmake -G 'Unix Makefiles' ../../
make
```

To build with Emscripten
```
make -f EmscriptenMakefile
```
See the makefile for more details.


Dependencies
------------

- Assimp and CGAL
- glfw, glew (example only)


References and resources
------------------------

[1] http://doc.cgal.org/latest/AABB_tree/index.html#Chapter_3D_Fast_Intersection_and_Distance_Computation
