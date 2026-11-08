#include "Application.h"
#include "IO.h"
#include "UI.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <filesystem>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#endif
#endif
#include <stdexcept>
#include <vector>

namespace mmw = MikuMikuWorld;
mmw::Application app;

#ifdef __APPLE__
@interface MMWMacApplicationDelegate : NSObject<NSApplicationDelegate>
@end

@implementation MMWMacApplicationDelegate
- (void)appendOpenFilename:(NSString*)filename
{
	if (filename != nil)
		app.appendOpenFile(std::string([filename UTF8String]));
}

- (void)application:(NSApplication*)application openFiles:(NSArray<NSString*>*)filenames
{
	for (NSString* filename in filenames)
		[self appendOpenFilename:filename];

	[application replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)handleOpenDocumentsEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)replyEvent
{
	NSAppleEventDescriptor* documents = [event paramDescriptorForKeyword:keyDirectObject];
	for (NSInteger index = 1; index <= documents.numberOfItems; ++index)
	{
		NSAppleEventDescriptor* item = [documents descriptorAtIndex:index];
		NSString* filename = nil;

		if ([item respondsToSelector:@selector(fileURLValue)])
		{
			NSURL* fileUrl = [item fileURLValue];
			filename = fileUrl.path;
		}

		if (filename == nil)
			filename = item.stringValue;

		[self appendOpenFilename:filename];
	}
}
@end

static MMWMacApplicationDelegate* macApplicationDelegate = nil;

@interface MMWAboutTarget : NSObject
- (void)showAbout:(id)sender;
@end

@implementation MMWAboutTarget
- (void)showAbout:(id)sender
{
	mmw::Application::aboutRequested = true;
}
@end

static MMWAboutTarget* aboutTarget = nil;

bool hookMacAboutMenu()
{
	@autoreleasepool
	{
		NSMenu* mainMenu = [NSApp mainMenu];
		if (mainMenu == nil || mainMenu.numberOfItems == 0)
			return false;
		NSMenu* appMenu = [[mainMenu itemAtIndex:0] submenu];
		for (NSMenuItem* item in appMenu.itemArray)
		{
			if (item.action == @selector(orderFrontStandardAboutPanel:) || item.action == @selector(showAbout:))
			{
				if (aboutTarget == nil)
					aboutTarget = [MMWAboutTarget new];
				item.target = aboutTarget;
				item.action = @selector(showAbout:);
				return true;
			}
		}
		return false;
	}
}

void setMacDockIcon(const std::string& path)
{
	@autoreleasepool
	{
		NSImage* image = path.empty() ? nil : [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
		[NSApp setApplicationIconImage:image];
	}
}
#endif

static int runApplication(int argc, const std::string& appPath, const std::vector<std::string>& openFiles)
{
	try
	{
		std::string dir = IO::File::getFilepath(appPath);
		mmw::Result result = app.initialize(dir);
		
		if (!result.isOk())
			throw std::runtime_error(result.getMessage());

		for (const auto& file : openFiles)
			app.appendOpenFile(file);

		app.handlePendingOpenFiles();
		app.run();
	}
	catch (const std::exception& ex)
	{
		IO::MessageBoxButtons actions = IO::MessageBoxButtons::Ok;
		std::string msg = "An unhandled exception has occurred and the application will now close.";
		if (!app.isEditorUpToDate())
		{
			msg.append("\nDo you want to save the current score?");
			actions = IO::MessageBoxButtons::YesNo;
		}

		msg
			.append("\n\nError: ")
			.append(ex.what())
			.append("\nApplication Version: ")
			.append(mmw::Application::getAppVersion());
			
		IO::MessageBoxResult result = IO::messageBox(APP_NAME, msg, actions, IO::MessageBoxIcon::Error, mmw::Application::windowState.windowHandle);
		if (!app.isEditorUpToDate() && result == IO::MessageBoxResult::Yes && app.attemptSave())
		{
			IO::messageBox(APP_NAME, "Save successful", IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Information, mmw::Application::windowState.windowHandle);
		}

		app.writeSettings();
	}

	app.dispose();
	return 0;
}

#ifdef _WIN32
int main()
{
	int argc;
	LPWSTR* args;
	args = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!args)
	{
		IO::messageBox(APP_NAME, "CommandLineToArgvW failed...", IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
		return 1;
	}

	std::vector<std::string> openFiles;
	for (int i = 1; i < argc; ++i)
		openFiles.push_back(IO::wideStringToMb(args[i]));
	mmw::Application::automation = mmw::AutomationOptions::parse(openFiles);

	const int result = runApplication(argc, IO::wideStringToMb(args[0]), openFiles);
	LocalFree(args);
	return result;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_TIMER:
		if (mmw::Application::windowState.windowDragging && wParam == mmw::Application::windowState.windowTimerId)
		{
			// grabbing the glfw window blocks the message queue causing the application to stop rendering
			// so we handle the message ourselves and update the UI explicitly
			if (app.getGlfwWindow())
				app.update();

			return 0;
		}
		break;

	case WM_ENTERSIZEMOVE:
		mmw::Application::windowState.windowDragging = true;
		break;

	case WM_EXITSIZEMOVE:
		mmw::Application::windowState.windowDragging = false;
		break;

	case WM_DROPFILES:
		if (HDROP dropHandle = reinterpret_cast<HDROP>(wParam); dropHandle != NULL)
		{
			const UINT filesCount = ::DragQueryFileW(dropHandle, 0xFFFFFFFF, NULL, 0u);
			for (UINT i = 0; i < filesCount; ++i)
			{
				const UINT bufferSize = ::DragQueryFileW(dropHandle, i, NULL, 0u);
				if (bufferSize > 0)
				{
					std::wstring wFilename(bufferSize + 1, 0);
					if (::DragQueryFileW(dropHandle, i, wFilename.data(), static_cast<UINT>(wFilename.size())) != 0)
						app.appendOpenFile(IO::wideStringToMb(wFilename.data()));
				}
			}

			::DragFinish(dropHandle);
		}
		break;
	case WM_SETTINGCHANGE:
		if (lParam != 0 && lstrcmpW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
		{
			mmw::UI::setDarkMode(mmw::UI::isSystemDarkMode());
		}
		break;

	default:
		// we don't handle this message ourselves so delegate it to the original glfw window's proc
		return CallWindowProcW((WNDPROC)glfwGetWindowUserPointer(app.getGlfwWindow()), hwnd, uMsg, wParam, lParam);
	}

	return ::DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
#else
int main(int argc, char** argv)
{
#ifdef __APPLE__
	@autoreleasepool
	{
		NSApplication* application = [NSApplication sharedApplication];
		macApplicationDelegate = [MMWMacApplicationDelegate new];
		[application setDelegate:macApplicationDelegate];
		[[NSAppleEventManager sharedAppleEventManager]
			setEventHandler:macApplicationDelegate
			andSelector:@selector(handleOpenDocumentsEvent:withReplyEvent:)
			forEventClass:kCoreEventClass
			andEventID:kAEOpenDocuments];
	}
#endif

	std::vector<std::string> openFiles;
	for (int i = 1; i < argc; ++i)
		openFiles.emplace_back(argv[i]);
	mmw::Application::automation = mmw::AutomationOptions::parse(openFiles);

	std::filesystem::path appPath = argc > 0 ? std::filesystem::absolute(argv[0]) : std::filesystem::current_path();
	return runApplication(argc, appPath.string(), openFiles);
}
#endif
