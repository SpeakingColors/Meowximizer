// Plugin entry point.
#include "common/FxBase.h"
#include "maximizer/Meowximizer.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Meowximizer(); }
