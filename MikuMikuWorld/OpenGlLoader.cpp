#define STB_IMAGE_IMPLEMENTATION
#ifdef _WIN32
#define STBI_WINDOWS_UTF8
#endif

#include "Application.h"
#include "ApplicationConfiguration.h"
#include "UI.h"
#include "stb_image.h"

namespace MikuMikuWorld
{
	void frameBufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		glViewport(0, 0, width, height);
	}

	void windowSizeCallback(GLFWwindow* window, int width, int height)
	{
		if (!Application::windowState.maximized && !Application::windowState.fullScreen)
		{
			Application::windowState.size.x = width;
			Application::windowState.size.y = height;
		}
	}

	void windowPositionCallback(GLFWwindow* window, int x, int y)
	{
		if (!Application::windowState.maximized && !Application::windowState.fullScreen)
		{
			Application::windowState.position.x = x;
			Application::windowState.position.y = y;
		}
	}

	void windowCloseCallback(GLFWwindow* window)
	{
		glfwSetWindowShouldClose(window, 0);
		Application::windowState.closing = true;
	}

	void windowMaximizeCallback(GLFWwindow* window, int _maximized)
	{
		Application::windowState.maximized = _maximized;
	}

	Result Application::initOpenGL()
	{
		// GLFW initializion
		const char* glfwErrorDescription = NULL;
		int possibleError = GLFW_NO_ERROR;

		glfwInit();
		possibleError = glfwGetError(&glfwErrorDescription);
		if (possibleError != GLFW_NO_ERROR)
		{
			glfwTerminate();
			return Result(ResultStatus::Error, "Failed to initialize GLFW.\n" + std::string(glfwErrorDescription));
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		int width{ static_cast<int>(config.windowSize.x) }, height{ static_cast<int>(config.windowSize.y) };

		if (config.fullScreen)
		{
			GLFWmonitor* mainMonitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(mainMonitor);

			width = mode->width, height = mode->height;
			window = glfwCreateWindow(width, height, APP_NAME, mainMonitor, NULL);
		}
		else
		{
			window = glfwCreateWindow(width, height, APP_NAME, NULL, NULL);
		}

		possibleError = glfwGetError(&glfwErrorDescription);
		if (possibleError != GLFW_NO_ERROR)
		{
			glfwTerminate();
			return Result(ResultStatus::Error, "Failed to create GLFW Window.\n" + std::string(glfwErrorDescription));
		}

		// a window saved on a monitor that is gone (or a smaller resolution) would open off screen
		int posX = static_cast<int>(config.windowPos.x), posY = static_cast<int>(config.windowPos.y);
		if (!config.fullScreen)
		{
			int monitorCount = 0;
			GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
			bool visible = false;
			for (int i = 0; i < monitorCount; ++i)
			{
				int mx, my, mw, mh;
				glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
				if (posX + 80 > mx && posX < mx + mw - 80 && posY >= my - 10 && posY < my + mh - 60)
					visible = true;
			}
			if (!visible && monitorCount > 0)
			{
				int mx, my, mw, mh;
				glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &mx, &my, &mw, &mh);
				width = std::min(width, mw - 40);
				height = std::min(height, mh - 40);
				glfwSetWindowSize(window, width, height);
				posX = mx + (mw - width) / 2;
				posY = my + (mh - height) / 2;
			}
		}
		glfwSetWindowPos(window, posX, posY);
		
		glfwMakeContextCurrent(window);
		glfwSetWindowTitle(window, APP_NAME " - Untitled");
		glfwSetWindowPosCallback(window, windowPositionCallback);
		glfwSetWindowSizeCallback(window, windowSizeCallback);
		glfwSetFramebufferSizeCallback(window, frameBufferResizeCallback);
		glfwSetWindowCloseCallback(window, windowCloseCallback);
		glfwSetWindowMaximizeCallback(window, windowMaximizeCallback);
#ifndef _WIN32
		glfwSetWindowUserPointer(window, this);
		glfwSetDropCallback(window, [](GLFWwindow* window, int count, const char** paths)
		{
			auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
			if (!app)
				return;

			for (int i = 0; i < count; ++i)
				app->appendOpenFile(paths[i]);
		});
#endif

		UI::setDarkMode(UI::isSystemDarkMode());

		// GLAD initializtion
		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			glfwTerminate();
			return Result(ResultStatus::Error, "Failed to fetch OpenGL proc address.");
		}

		glfwSwapInterval(config.vsync);
		if (config.maximized && !config.fullScreen)
			glfwMaximizeWindow(window);

		loadAppIcon();

		glLineWidth(1.0f);
		glPointSize(1.0f);
		glEnablei(GL_BLEND, 0);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glViewport(0, 0, width, height);

		return Result::Ok();
	}

	void Application::loadAppIcon()
	{
		appliedIconPath = config.appIconPath;
		appIconLoaded = true;
		if (appIconTexture)
		{
			glDeleteTextures(1, &appIconTexture);
			appIconTexture = 0;
		}

		std::string file = config.appIconPath;
		const bool custom = !file.empty() && IO::File::exists(file);
		if (!custom)
			file = appDir + "res/mmw_icon.png";

		int width = 0, height = 0;
		unsigned char* pixels = stbi_load(file.c_str(), &width, &height, nullptr, 4);
		if (!pixels)
			return;

		GLFWimage image{ width, height, pixels };
#ifndef __APPLE__
		glfwSetWindowIcon(window, 1, &image);
#endif
		{
			// the menu bar logo draws the icon too
			glGenTextures(1, &appIconTexture);
			glBindTexture(GL_TEXTURE_2D, appIconTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			glGenerateMipmap(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		stbi_image_free(pixels);
#ifdef __APPLE__
		setMacDockIcon(custom ? file : std::string{});
#endif
	}
}
