/* Widget visibility has an explicit storage lifetime. LGPL-3.0-or-later. */
#include "ImageLifetime.h"
#include "widget/WidgetContainer.h"
#include "widget/Widget.h"
namespace Sexy {
void H1ImageResourcesReleaseWidgetTree(WidgetContainer* widget){
    if(!widget)return;
    for(auto* child:widget->mWidgets)H1ImageResourcesReleaseWidgetTree(child);
    H1ImageResourcesReleaseOwner(H1ImageOwner::Widget,widget);
}
}
