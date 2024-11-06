# SceneDebugger
Lightweight visualizer/debugger of 3D data with a multi-frame capability. You just need to have something like this in your clipboard and then you just hit the Paste From Clipboard button in the app gui:

```
framestart()
drawtriangle "tri1" [3068.184376,-479.325038,1088.290321][3074.934611,-481.091150,1088.166570][3076.687322,-474.356001,1087.650898]
drawtriangle "tri2" [3068.434316,-478.857318,1095.248691][3068.184376,-479.325038,1088.290321][3074.934611,-481.091150,1088.166570]
frameend()
framestart()
drawline "line1" [3150.919551,-434.003753,1116.440534][3155.048861,-443.098191,1116.930409]
frameend()
framestart()
drawtriangle "tri3" [3084.849128,-481.484160,1074.279908][3078.061153,-479.866765,1074.366861][3079.668728,-473.093633,1073.876423]
drawline "line2" [3167.384383,-432.118211,1124.298729][3170.572222,-441.578575,1123.716327] 
drawline "line3" [3160.443098,-429.762951,1123.611836][3163.630937,-439.223315,1123.029435] 
frameend()
```

## Build Steps:

1. Modify these paths in CMakeLists.txt to where you've installed the libraries
```
set(GLFW_ROOT "C:/Devel/glfw-3.3.8.bin.WIN64")
set(GLEW_ROOT "C:/Devel/glew-2.1.0")
set(GLM_ROOT "C:/Devel/glm")
```

2. Copy imgui (imgui-1.91.4.zip should certainly work) to SceneDebugger root directory
3. Use CMake as usual
4. If launching from Visual Studio, do not forget to set the Debugging Working Directory correctly, otherwise the shader files will not be loaded correctly.
