param([Parameter(Mandatory=$true)][ValidateSet('cutscene','arm','weapon')][string]$Mode)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class GloryAtlasExport {
 public static void Export(string input,string sourcePath,string palettePath,string output,string mode) {
  var lines=System.IO.File.ReadAllLines(palettePath);
  using(var image=new Bitmap(input))using(var source=new Bitmap(sourcePath))using(var dst=new Bitmap(128,128,PixelFormat.Format4bppIndexed)) {
   var pal=dst.Palette;
   for(int k=0;k<16;k++){var c=lines[k+3].Split(new char[]{' ','\t'},StringSplitOptions.RemoveEmptyEntries);pal.Entries[k]=Color.FromArgb(k==0?0:255,int.Parse(c[0]),int.Parse(c[1]),int.Parse(c[2]));}
   dst.Palette=pal;
   var data=dst.LockBits(new Rectangle(0,0,128,128),ImageLockMode.WriteOnly,PixelFormat.Format4bppIndexed);
   byte[] raw=new byte[data.Stride*128];
   for(int y=0;y<128;y++)for(int x=0;x<128;x++) {
    if(mode=="cutscene" && !(y<32 || (y<48 && x<24) || (y>=32 && y<64 && x>=24 && x<120) || (y>=48 && y<64 && x<16)))continue;
    var original=source.GetPixel(x,y);
    if(mode=="arm" && original.A<180)continue;
    var c=image.GetPixel((2*x+1)*image.Width/256,(2*y+1)*image.Height/256);
    if(c.A<180){if(mode!="arm")continue;c=original;if(c.B>c.R&&c.B>c.G)c=pal.Entries[c.B>150?6:9];}
    int best=1;long distance=long.MaxValue;
    for(int k=1;k<16;k++){var p=pal.Entries[k];long dr=c.R-p.R,dg=c.G-p.G,db=c.B-p.B,d=dr*dr+dg*dg+db*db;if(d<distance){distance=d;best=k;}}
    // The flame layer leaves its white core open for the rigid blade below.
    if(mode=="weapon" && y>=64 && y<96 && best==6)continue;
    raw[y*data.Stride+x/2]|=(byte)(best<<((x%2==0)?4:0));
   }
   Marshal.Copy(raw,0,data.Scan0,raw.Length);dst.UnlockBits(data);dst.Save(output,ImageFormat.Png);
  }
 }
}
'@
$repo=(Resolve-Path "$PSScriptRoot/../../../..").Path
$source=if($Mode -eq 'arm'){"$PSScriptRoot/arm_source.png"}else{"$repo/assets/ric/jesus_ferido_cutscene.png"}
$name=@{cutscene='cutscene';arm='braco';weapon='espada'}[$Mode]
[GloryAtlasExport]::Export("$PSScriptRoot/$($Mode)_generated.png",$source,"$repo/assets/ric/jesus_glorificado_palette.pal","$repo/assets/ric/jesus_glorificado_$name.png",$Mode)
