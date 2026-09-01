#include "amt/mastering/DesktopMastering.h"

// Keep the established Windows UI/workflow shell while routing its mastering call
// through the Phase 5 adapter. OfflineRenderer.h has already been included by the
// adapter header, so this macro only redirects the app call sites in Phase4Main.cpp.
#define render_mastering_plan render_mastering_plan_for_desktop
#include "Phase4Main.cpp"
#undef render_mastering_plan
