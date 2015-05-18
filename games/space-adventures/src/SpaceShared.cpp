#include <gameplay.h>

#include "SpaceShared.h"


using namespace space;

Font *SpaceShared::little_;
Font *SpaceShared::medium_;
Font *SpaceShared::big_;

void SpaceShared::load()
{
    little_ = Font::create("res/ui/little.gpb");
    GP_ASSERT(little_ != NULL);
    medium_ = Font::create("res/ui/medium.gpb");
    GP_ASSERT(medium_ != NULL);
    big_ = Font::create("res/ui/big.gpb");
    GP_ASSERT(big_ != NULL);
}

void SpaceShared::unload()
{
    SAFE_RELEASE(little_);
    SAFE_RELEASE(medium_);
    SAFE_RELEASE(big_);
}

Font *SpaceShared::littleFont()
{
    return little_;
}

Font *SpaceShared::mediumFont()
{
    return medium_;
}

Font *SpaceShared::bigFont()
{
    return big_;
}


