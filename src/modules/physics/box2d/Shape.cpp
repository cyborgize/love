/**
* Copyright (c) 2006-2011 LOVE Development Team
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
*
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
*
* 1. The origin of this software must not be misrepresented; you must not
*    claim that you wrote the original software. If you use this software
*    in a product, an acknowledgment in the product documentation would be
*    appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
*    misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
**/

#include "Shape.h"

// Module
#include "Body.h"
#include "World.h"

// STD
#include <bitset>

namespace love
{
namespace physics
{
namespace box2d
{
	Shape::Shape(World * world)
		: world(world), shape(NULL)
	{
	}

	Shape::~Shape()
	{
		shape = 0;
	}

	Shape::Type Shape::getType() const
	{
		switch(shape->GetType())
		{
		case b2Shape::e_circle:
			return SHAPE_CIRCLE;
		case b2Shape::e_polygon:
			return SHAPE_POLYGON;
		case b2Shape::e_edge:
			return SHAPE_EDGE;
		case b2Shape::e_chain:
			return SHAPE_CHAIN;
		default:
			return SHAPE_INVALID;
		}
	}
	
	float Shape::getRadius() const
	{
		return world->scaleUp(shape->m_radius);
	}
	
	int Shape::getChildCount() const
	{
		return shape->GetChildCount();
	}
	
	bool Shape::testPoint(float x, float y, float r, float px, float py) const
	{
		b2Vec2 point(px, py);
		b2Transform transform(world->scaleDown(b2Vec2(x, y)), b2Rot(r));
		return shape->TestPoint(transform, world->scaleDown(point));
	}
	
	int Shape::rayCast(lua_State * L) const
	{
		float p1x = world->scaleDown((float)luaL_checknumber(L, 1));
		float p1y = world->scaleDown((float)luaL_checknumber(L, 2));
		float p2x = world->scaleDown((float)luaL_checknumber(L, 3));
		float p2y = world->scaleDown((float)luaL_checknumber(L, 4));
		float maxFraction = (float)luaL_checknumber(L, 5);
		float x = world->scaleDown((float)luaL_checknumber(L, 6));
		float y = world->scaleDown((float)luaL_checknumber(L, 7));
		float r = (float)luaL_checknumber(L, 8);
		int childIndex = (int)luaL_optint(L, 9, 0);
		b2RayCastInput input;
		input.p1.Set(p1x, p1y);
		input.p2.Set(p2x, p2y);
		input.maxFraction = maxFraction;
		b2Transform transform(b2Vec2(x, y), b2Rot(r));
		b2RayCastOutput output;
		shape->RayCast(&output, input, transform, childIndex);
		lua_pushnumber(L, world->scaleUp(output.normal.x));
		lua_pushnumber(L, world->scaleUp(output.normal.y));
		lua_pushnumber(L, output.fraction);
		return 3;
	}
	
	int Shape::computeAABB(lua_State * L) const
	{
		float x = world->scaleDown((float)luaL_checknumber(L, 1));
		float y = world->scaleDown((float)luaL_checknumber(L, 2));
		float r = (float)luaL_checknumber(L, 3);
		int childIndex = (int)luaL_optint(L, 4, 0);
		b2Transform transform(b2Vec2(x, y), b2Rot(r));
		b2AABB box;
		shape->ComputeAABB(&box, transform, childIndex);
		box = world->scaleUp(box);
		lua_pushnumber(L, box.lowerBound.x);
		lua_pushnumber(L, box.lowerBound.y);
		lua_pushnumber(L, box.upperBound.x);
		lua_pushnumber(L, box.upperBound.y);
		return 4;
	}
	
	int Shape::computeMass(lua_State * L) const
	{
		float density = world->scaleDown((float)luaL_checknumber(L, 1));
		b2MassData data;
		shape->ComputeMass(&data, density);
		b2Vec2 center = world->scaleUp(data.center);
		lua_pushnumber(L, center.x);
		lua_pushnumber(L, center.y);
		lua_pushnumber(L, data.mass);
		lua_pushnumber(L, data.I);
		return 4;
	}

} // box2d
} // physics
} // love
