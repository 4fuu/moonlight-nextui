#ifndef __FN_ACTION_H__
#define __FN_ACTION_H__

#include <stddef.h>
#include <string.h>

// User-assignable button actions are stored in fn[1-3]action as
// "<kind>" or "<kind>:<arg>". The empty string is the legacy "not set" value and
// falls back to the per-slot default below; "none" is an explicit unassignment,
// so a user can still turn a defaulted button off.
#define FN_ACTION_NONE "none"
#define FN_ACTION_HOME "home"
#define FN_ACTION_OVERLAY "overlay"
#define FN_ACTION_PAK_PREFIX "pak:"

typedef enum
{
	FN_ACTION_KIND_NONE = 0, // unassigned (legacy empty or explicit "none")
	FN_ACTION_KIND_PAK,      // launcher action: run Tools/<PLATFORM>/<arg>/launch.sh
	FN_ACTION_KIND_HOME,     // app-runtime action: leave the foreground app
	FN_ACTION_KIND_OVERLAY,  // app-runtime action: open the system HOME overlay
	FN_ACTION_KIND_UNKNOWN,  // written by a newer build; ignore
} FnActionKind;

typedef enum
{
	// The launcher owns the foreground: there is no app to exit and no app
	// process in which to draw the shared overlay.
	FN_ACTION_SCOPE_DESKTOP = 0,
	// An application is in the foreground and can host the system actions.
	FN_ACTION_SCOPE_APP,
} FnActionScope;

typedef enum
{
	FN_ACTION_DISPATCH_NONE = 0,    // unassigned / explicit "none"
	FN_ACTION_DISPATCH_HANDLED,     // a handler ran and reported success
	FN_ACTION_DISPATCH_FAILED,      // a handler ran and reported failure
	FN_ACTION_DISPATCH_UNSUPPORTED, // valid action, but not executable in this scope/build
	FN_ACTION_DISPATCH_UNKNOWN,     // unknown kind: ignore for forward compatibility
} FnActionDispatchResult;

// Handlers are supplied by the layer that owns the foreground. A2 only wires
// launch_pak; exit_to_home and open_overlay stay NULL until the app-runtime
// integration lands, so dispatch reports UNSUPPORTED instead of pretending they
// ran. This is also why no handler may receive a raw action string: only the
// validated pak path reaches a handler.
typedef struct
{
	void* user;
	int (*launch_pak)(void* user, const char* relative_path);
	int (*exit_to_home)(void* user);
	int (*open_overlay)(void* user);
} FnActionHandlers;

// True when relative_path stays inside Tools/<PLATFORM>: non-empty, relative,
// no empty/"."/".." segments, no backslashes or control characters.
static inline int fnActionPakPathIsSafe(const char* relative_path)
{
	if (!relative_path || !relative_path[0]) return 0;
	if (relative_path[0] == '/' || relative_path[0] == '\\') return 0;
	if (relative_path[strlen(relative_path) - 1] == '/') return 0;

	const char* segment = relative_path;
	while (*segment)
	{
		const char* slash = strchr(segment, '/');
		size_t len = slash ? (size_t)(slash - segment) : strlen(segment);
		if (len == 0) return 0; // leading "//" or trailing "/"
		if (len == 1 && segment[0] == '.') return 0;
		if (len == 2 && segment[0] == '.' && segment[1] == '.') return 0;
		for (size_t i = 0; i < len; i++)
		{
			unsigned char c = (unsigned char)segment[i];
			if (c < 0x20 || c == 0x7f || c == '\\') return 0;
		}
		if (!slash) break;
		segment = slash + 1;
	}
	return 1;
}

// Parses a stored action. `arg` is set to the pak path for FN_ACTION_KIND_PAK
// and to NULL otherwise. Kinds are matched exactly, so "home:1" is unknown.
static inline FnActionKind fnActionParse(const char* action, const char** arg)
{
	if (arg) *arg = NULL;
	if (!action || !action[0]) return FN_ACTION_KIND_NONE;
	if (strcmp(action, FN_ACTION_NONE) == 0) return FN_ACTION_KIND_NONE;
	if (strcmp(action, FN_ACTION_HOME) == 0) return FN_ACTION_KIND_HOME;
	if (strcmp(action, FN_ACTION_OVERLAY) == 0) return FN_ACTION_KIND_OVERLAY;

	const size_t prefix_len = sizeof(FN_ACTION_PAK_PREFIX) - 1;
	if (strncmp(action, FN_ACTION_PAK_PREFIX, prefix_len) == 0)
	{
		const char* relative_path = action + prefix_len;
		if (!fnActionPakPathIsSafe(relative_path)) return FN_ACTION_KIND_UNKNOWN;
		if (arg) *arg = relative_path;
		return FN_ACTION_KIND_PAK;
	}

	return FN_ACTION_KIND_UNKNOWN;
}

// Built-in defaults by button slot: FN1 unassigned, FN2 exits to the desktop,
// FN3 (HOME) opens the system overlay. Stored empty values fall back to these so
// cards written before these actions existed keep the intended behavior.
static inline const char* fnActionDefault(int index)
{
	if (index == 1) return FN_ACTION_HOME;
	if (index == 2) return FN_ACTION_OVERLAY;
	return "";
}

static inline const char* fnActionEffective(int index, const char* configured)
{
	if (configured && configured[0]) return configured;
	return fnActionDefault(index);
}

// Routes one action to the handler that owns the current foreground. This is
// the safe boundary: an action is only executed when its kind is valid for the
// scope and the corresponding handler is present. Returns UNSUPPORTED rather
// than inventing behavior for actions whose implementation has not landed.
static inline FnActionDispatchResult fnActionDispatch(const char* action,
													   FnActionScope scope,
													   const FnActionHandlers* handlers)
{
	const char* arg = NULL;
	FnActionKind kind = fnActionParse(action, &arg);

	switch (kind)
	{
	case FN_ACTION_KIND_NONE:
		return FN_ACTION_DISPATCH_NONE;

	case FN_ACTION_KIND_PAK:
		if (scope != FN_ACTION_SCOPE_DESKTOP || !handlers || !handlers->launch_pak)
			return FN_ACTION_DISPATCH_UNSUPPORTED;
		return handlers->launch_pak(handlers->user, arg)
				   ? FN_ACTION_DISPATCH_HANDLED
				   : FN_ACTION_DISPATCH_FAILED;

	case FN_ACTION_KIND_HOME:
		if (scope != FN_ACTION_SCOPE_APP || !handlers || !handlers->exit_to_home)
			return FN_ACTION_DISPATCH_UNSUPPORTED;
		return handlers->exit_to_home(handlers->user)
				   ? FN_ACTION_DISPATCH_HANDLED
				   : FN_ACTION_DISPATCH_FAILED;

	case FN_ACTION_KIND_OVERLAY:
		if (scope != FN_ACTION_SCOPE_APP || !handlers || !handlers->open_overlay)
			return FN_ACTION_DISPATCH_UNSUPPORTED;
		return handlers->open_overlay(handlers->user)
				   ? FN_ACTION_DISPATCH_HANDLED
				   : FN_ACTION_DISPATCH_FAILED;

	case FN_ACTION_KIND_UNKNOWN:
	default:
		return FN_ACTION_DISPATCH_UNKNOWN;
	}
}

#endif
