set(N3BASE_COMMON_AUDIO_SOURCES
  al_wrapper.h
  AudioAsset.cpp
  AudioAsset.h
  AudioDecoderThread.cpp
  AudioDecoderThread.h
  AudioHandle.cpp
  AudioHandle.h
  AudioThread.cpp
  AudioThread.h
  mpg123_reader_io.cpp
  mpg123_reader_io.h
  N3SndDef.h
  N3SndMgr.cpp
  N3SndMgr.h
  N3SndObj.cpp
  N3SndObj.h
)

set(N3BASE_COMMON_EFFECT_SOURCES
  N3FXBundle.cpp
  N3FXBundle.h
  N3FXDef.h
  N3FXPartBase.cpp
  N3FXPartBase.h
  N3FXPartBillBoard.cpp
  N3FXPartBillBoard.h
  N3FXPartBottomBoard.cpp
  N3FXPartBottomBoard.h
  N3FXParticle.cpp
  N3FXParticle.h
  N3FXPartMesh.cpp
  N3FXPartMesh.h
  N3FXPartParticles.cpp
  N3FXPartParticles.h
  N3FXPlug.cpp
  N3FXPlug.h
  N3FXPMesh.cpp
  N3FXPMesh.h
  N3FXPMeshInstance.cpp
  N3FXPMeshInstance.h
  N3FXShape.cpp
  N3FXShape.h
)

set(N3BASE_TOOL_EFFECT_SOURCES
  N3FXGroup.cpp
  N3FXGroup.h
)

set(N3BASE_COMMON_SKY_SOURCES
  N3Cloud.cpp
  N3Cloud.h
  N3Moon.cpp
  N3Moon.h
  N3Sky.cpp
  N3Sky.h
  N3SkyMng.cpp
  N3SkyMng.h
  N3Star.cpp
  N3Star.h
  N3Sun.cpp
  N3Sun.h
)

set(N3BASE_COMMON_UI_SOURCES
  N3UIArea.cpp
  N3UIArea.h
  N3UIBase.cpp
  N3UIBase.h
  N3UIButton.cpp
  N3UIButton.h
  N3UIDef.h
  N3UIEdit.cpp
  N3UIEdit.h
  N3UIImage.cpp
  N3UIImage.h
  N3UIList.cpp
  N3UIList.h
  N3UIProgress.cpp
  N3UIProgress.h
  N3UIScrollBar.cpp
  N3UIScrollBar.h
  N3UIStatic.cpp
  N3UIStatic.h
  N3UIString.cpp
  N3UIString.h
  N3UITooltip.cpp
  N3UITooltip.h
  N3UITrackBar.cpp
  N3UITrackBar.h
)

set(N3BASE_COMMON_WEATHER_SOURCES
  N3GERain.cpp
  N3GERain.h
  N3GESnow.cpp
  N3GESnow.h
  N3GlobalEffect.cpp
  N3GlobalEffect.h
)

set(N3BASE_TOOL_WEATHER_SOURCES
  N3GlobalEffectMng.cpp
  N3GlobalEffectMng.h
)

# The platform layer: OS services and the rendering API behind portable interfaces.
# Windows uses the original Win32 / Direct3D 9 calls; macOS (the port) its own implementations.
set(N3BASE_COMMON_PLATFORM_SOURCES
  Platform/Application.h
  Platform/Crypto.h
  Platform/FileSystem.cpp
  Platform/FileSystem.h
  Platform/FontRasterizer.h
  Platform/Input.h
  Platform/KeyCodes.h
  Platform/RenderApi.h
  Platform/ScreenCapture.h
  Platform/PlatformOptions.h
  Platform/Settings.h
  Platform/Shell.h
  Platform/Socket.h
  Platform/TextInput.h
  Platform/Time.h
  Platform/Types.h
  Platform/Window.h
)

if(WIN32)
  list(APPEND N3BASE_COMMON_PLATFORM_SOURCES
    Platform/Win32/Application_Win32.cpp
    Platform/Win32/Crypto_Win32.cpp
    Platform/Win32/FontRasterizer_Win32.cpp
    Platform/Win32/Input_Win32.cpp
    Platform/Win32/KeyboardDevice_Win32.cpp
    Platform/Win32/PlatformOptions_Win32.cpp
    Platform/Win32/ScreenCapture_Win32.cpp
    Platform/Win32/Settings_Win32.cpp
    Platform/Win32/Shell_Win32.cpp
    Platform/Win32/Socket_Win32.cpp
    Platform/Win32/TextInput_Win32.cpp
    Platform/Win32/Time_Win32.cpp
    Platform/Win32/Win32Internal.h
    Platform/Win32/Window_Win32.cpp
  )

  # winsock2.h has to precede <windows.h>, which the precompiled header includes.
  set_source_files_properties(Platform/Win32/Socket_Win32.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
elseif(APPLE)
  list(APPEND N3BASE_COMMON_PLATFORM_SOURCES
    Platform/Compat/D3D9.h
    Platform/Compat/DInputKeys.h
    Platform/Compat/Win32Types.cpp
    Platform/Compat/Win32Types.h
    Platform/macOS/AppBundle_macOS.cpp
    Platform/macOS/Application_macOS.cpp
    Platform/macOS/Cocoa/CocoaApp.h
    Platform/macOS/Cocoa/CocoaApp.mm
    Platform/macOS/Cocoa/CoreTextFont.cpp
    Platform/macOS/Cocoa/CoreTextFont.h
    Platform/macOS/Cocoa/MetalRenderer.h
    Platform/macOS/Cocoa/MetalRenderer.mm
    Platform/macOS/CodePage.cpp
    Platform/macOS/CodePage.h
    Platform/macOS/Crypto_macOS.cpp
    Platform/macOS/D3DX_macOS.cpp
    Platform/macOS/Direct3D9_macOS.cpp
    Platform/macOS/Direct3D9_macOS.h
    Platform/macOS/FontRasterizer_macOS.cpp
    Platform/macOS/Input_macOS.cpp
    Platform/macOS/InputScript.cpp
    Platform/macOS/InputScript.h
    Platform/macOS/KeyboardDevice_macOS.cpp
    Platform/macOS/MacState.h
    Platform/macOS/NotImplemented.h
    Platform/macOS/ScreenCapture_macOS.cpp
    Platform/macOS/Options_macOS.cpp
    Platform/macOS/Options_macOS.h
    Platform/macOS/PlatformOptions_macOS.cpp
    Platform/macOS/Settings_macOS.cpp
    Platform/macOS/Shell_macOS.cpp
    Platform/macOS/Socket_macOS.cpp
    Platform/macOS/TextInput_macOS.cpp
    Platform/macOS/Time_macOS.cpp
    Platform/macOS/Window_macOS.cpp
  )
endif()

if(APPLE)
  # The Cocoa side never sees the Win32 compat types (they clash with Objective-C's and
  # CoreFoundation's), so it stays out of the precompiled header, which has them.
  set_source_files_properties(
    Platform/macOS/Cocoa/CocoaApp.mm
    Platform/macOS/Cocoa/CoreTextFont.cpp
    Platform/macOS/Cocoa/MetalRenderer.mm
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  set_source_files_properties(
    Platform/macOS/Cocoa/CocoaApp.mm
    Platform/macOS/Cocoa/MetalRenderer.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

  set(N3BASE_APPLE_LIBS
    "-framework AppKit"
    "-framework Carbon"
    "-framework CoreText"
    "-framework ImageIO"
    "-framework IOKit"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers"
    asio
  )
endif()

set(N3BASE_COMMON_SOURCES
  BitMapFile.cpp
  BitMapFile.h
  ColorValue.inl
  DebugUtils.cpp
  DebugUtils.h
  DFont.cpp
  DFont.h
  LogWriter.cpp
  LogWriter.h
  Material.inl
  My_3DStruct.cpp
  My_3DStruct.h
  N3AlphaPrimitiveManager.cpp
  N3AlphaPrimitiveManager.h
  N3AnimControl.cpp
  N3AnimControl.h
  N3AnimKey.cpp
  N3AnimKey.h
  N3Base.cpp
  N3Base.h
  N3BaseFileAccess.cpp
  N3BaseFileAccess.h
  N3Camera.cpp
  N3Camera.h
  N3Chr.cpp
  N3Chr.h
  N3Cloak.cpp
  N3Cloak.h
  N3ColorChange.cpp
  N3ColorChange.h
  N3Eng.cpp
  N3Eng.h
  N3IMesh.cpp
  N3IMesh.h
  N3Joint.cpp
  N3Joint.h
  N3LayoutChecks.cpp
  N3Light.cpp
  N3Light.h
  N3Mesh.cpp
  N3Mesh.h
  N3Mng.h
  N3PMesh.cpp
  N3PMesh.h
  N3PMeshInstance.cpp
  N3PMeshInstance.h
  N3Scene.cpp
  N3Scene.h
  N3Shape.cpp
  N3Shape.h
  N3ShapeExtra.cpp
  N3ShapeExtra.h
  N3ShapeMgr.cpp
  N3ShapeMgr.h
  N3Skin.cpp
  N3Skin.h
  N3TableBase.h
  N3TableBaseImpl.cpp
  N3TableBaseImpl.h
  N3Texture.cpp
  N3Texture.h
  N3Transform.cpp
  N3Transform.h
  N3TransformCollision.cpp
  N3TransformCollision.h
  N3VMesh.cpp
  N3VMesh.h
  StdAfxBase.h
  WaveFormat.cpp
  WaveFormat.h
  WinCrypt.cpp
  WinCrypt.h
  VertexTypes.inl
  ${N3BASE_COMMON_AUDIO_SOURCES}
  ${N3BASE_COMMON_EFFECT_SOURCES}
  ${N3BASE_COMMON_SKY_SOURCES}
  ${N3BASE_COMMON_UI_SOURCES}
  ${N3BASE_COMMON_WEATHER_SOURCES}
  ${N3BASE_COMMON_PLATFORM_SOURCES}
)

set(N3BASE_COMMON_DEPENDENCIES_PUBLIC
  FileIO
  shared
  spdlog
)

# Only include the DX9 SDK with MSVC.
# mingw packages its own
if (MSVC)
  list(APPEND N3BASE_COMMON_DEPENDENCIES_PUBLIC dx9sdk)
endif ()

set(N3BASE_COMMON_DEPENDENCIES_PRIVATE
  MathUtils
  libmpg123_wrapper
  OpenAL
)

source_group("Audio" FILES ${N3BASE_COMMON_AUDIO_SOURCES})
source_group("Effects" FILES ${N3BASE_COMMON_EFFECT_SOURCES} ${N3BASE_TOOL_EFFECT_SOURCES})
source_group("Sky" FILES ${N3BASE_COMMON_SKY_SOURCES})
source_group("UI" FILES ${N3BASE_COMMON_UI_SOURCES})
source_group("Weather" FILES ${N3BASE_COMMON_WEATHER_SOURCES} ${N3BASE_TOOL_WEATHER_SOURCES})
source_group("Platform" FILES ${N3BASE_COMMON_PLATFORM_SOURCES})

# Link in the libs that we use
if(WIN32)
  set(N3BASE_COMMON_LIBS_PRIVATE
    d3d9.lib
    d3dx9.lib
    dinput8.lib
    dsound.lib
    dxguid.lib
  )
else()
  set(N3BASE_COMMON_LIBS_PRIVATE)
endif()

# Setup warning levels
set(N3BASE_COMMON_COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall>
  $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra>
)

if(OPENKO_BUILD_CLIENT)
  add_library(N3Base_client STATIC
    ${N3BASE_COMMON_SOURCES}
  )

  set_target_properties(N3Base_client PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  if (OPENKO_COMPILE_WARNINGS_AS_ERROR)
    set_property(TARGET N3Base_client PROPERTY COMPILE_WARNING_AS_ERROR ON)
  endif()

  target_compile_definitions(N3Base_client PUBLIC
    _N3GAME
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX)

  target_link_libraries(N3Base_client PUBLIC ${N3BASE_COMMON_DEPENDENCIES_PUBLIC})
  target_link_libraries(N3Base_client PRIVATE ${N3BASE_COMMON_LIBS_PRIVATE})
  target_link_libraries(N3Base_client PRIVATE ${N3BASE_COMMON_DEPENDENCIES_PRIVATE})
  if(APPLE)
    target_link_libraries(N3Base_client PRIVATE ${N3BASE_APPLE_LIBS})
  endif()

  target_include_directories(N3Base_client INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../" # so we can access <N3Base/...>
  )

  target_precompile_headers(N3Base_client PRIVATE "StdAfxBase.h")
  target_compile_options(N3Base_client PRIVATE ${N3BASE_COMMON_COMPILE_OPTIONS})
endif()

if(OPENKO_BUILD_TOOLS)
  add_library(N3Base_tool STATIC
    N3EngTool.cpp
    N3EngTool.h
    N3PMeshCreate.cpp
    N3PMeshCreate.h
    Pick.cpp
    Pick.h
    ${N3BASE_COMMON_SOURCES}
    ${N3BASE_TOOL_EFFECT_SOURCES}
    ${N3BASE_TOOL_WEATHER_SOURCES}
  )

  set_target_properties(N3Base_tool PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  if (OPENKO_COMPILE_WARNINGS_AS_ERROR)
    set_property(TARGET N3Base_tool PROPERTY COMPILE_WARNING_AS_ERROR ON)
  endif()

  target_compile_definitions(N3Base_tool PUBLIC
    _N3TOOL
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX)

  target_compile_definitions(N3Base_tool INTERFACE
    NO_WARN_MBCS_MFC_DEPRECATION)

  target_link_libraries(N3Base_tool PUBLIC ${N3BASE_COMMON_DEPENDENCIES_PUBLIC})
  target_link_libraries(N3Base_tool PRIVATE ${N3BASE_COMMON_LIBS_PRIVATE})
  target_link_libraries(N3Base_tool PRIVATE ${N3BASE_COMMON_DEPENDENCIES_PRIVATE})

  target_include_directories(N3Base_tool INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../" # so we can access <N3Base/...>
  )

  target_precompile_headers(N3Base_tool PRIVATE "StdAfxBase.h")
  target_compile_options(N3Base_tool PRIVATE ${N3BASE_COMMON_COMPILE_OPTIONS})
endif()
