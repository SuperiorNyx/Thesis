﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Audio;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.GamerServices;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using Microsoft.Xna.Framework.Media;

namespace Thesis
{
    class Player: Entity
    {
        public Player(Texture2D spriteSheet, Vector2 startPos): base(spriteSheet, startPos)
        {
        }

        new public void Update(GameTime gameTime)
        {
        }

        public void resolveCollide(WObject o)
        {
        }

    }
}