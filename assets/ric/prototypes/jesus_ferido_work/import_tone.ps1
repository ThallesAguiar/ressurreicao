param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class ToneImport {
 public static void Frame(string sheetPath,string sourcePath,string outputPath,int cell) {
  using(var sheet=new Bitmap(sheetPath)) using(var source=new Bitmap(sourcePath)) {
   using(var output=new Bitmap(source.Width,source.Height,PixelFormat.Format4bppIndexed)) {
    int[] rgb={0,0x211716,0x493023,0x704932,0x9a6745,0xc48e5d,0xf1bb86,0xffefd0,0xe2d4b4,0xb7aa91,0x8b8276,0x5d5955,0x42191b,0x7e2425,0xb44637,0xc6c8c1};
    var palette=output.Palette;
    for(int k=0;k<16;k++) palette.Entries[k]=Color.FromArgb(k==0?0:255,(rgb[k]>>16)&255,(rgb[k]>>8)&255,rgb[k]&255);
    output.Palette=palette;
    var data=output.LockBits(new Rectangle(0,0,output.Width,output.Height),ImageLockMode.WriteOnly,PixelFormat.Format4bppIndexed);
    byte[] bytes=new byte[data.Stride*output.Height];
    int ox=(cell%8)*192+(192-source.Width*4)/2,oy=(cell/8)*320+40;
    for(int y=0;y<source.Height;y++) for(int x=0;x<source.Width;x++) {
     var original=source.GetPixel(x,y); if(original.A==0) continue;
     int sx=(ox+x*4+2)*sheet.Width/1536,sy=(oy+y*4+2)*sheet.Height/1024;
     var c=sheet.GetPixel(sx,sy);
     bool edge=x==0||y==0||x==source.Width-1||y==source.Height-1;
     if(!edge) edge=source.GetPixel(x-1,y).A==0||source.GetPixel(x+1,y).A==0||source.GetPixel(x,y-1).A==0||source.GetPixel(x,y+1).A==0;
     // Preserve the generated dark contour when reducing its expanded edge
     // back into the exact native silhouette. Ignore the blue-gray backdrop.
     if(edge) {
      int radius=Math.Max(1,3*sheet.Width/1536);
      for(int dy=-radius;dy<=radius;dy++) for(int dx=-radius;dx<=radius;dx++) {
       int px=sx+dx,py=sy+dy;if(px<0||py<0||px>=sheet.Width||py>=sheet.Height) continue;
       var candidate=sheet.GetPixel(px,py);
       if(candidate.R>candidate.B && candidate.R*3+candidate.G*6+candidate.B<c.R*3+c.G*6+c.B) c=candidate;
      }
     }
     // Keep the native silhouette even if generation moved an edge slightly.
     if(Math.Abs(c.R-36)+Math.Abs(c.G-38)+Math.Abs(c.B-46)<24 || c.A<128) c=original;
     int index=1; long best=long.MaxValue;
     for(int k=1;k<16;k++) {var p=palette.Entries[k]; long dr=c.R-p.R,dg=c.G-p.G,db=c.B-p.B;long distance=dr*dr+dg*dg+db*db;if(distance<best){best=distance;index=k;}}
     bytes[y*data.Stride+x/2]|=(byte)(index<<((x%2==0)?4:0));
    }
    Marshal.Copy(bytes,0,data.Scan0,bytes.Length);output.UnlockBits(data);output.Save(outputPath,ImageFormat.Png);
   }
  }
 }
}
'@
$repo = (Resolve-Path "$PSScriptRoot/../../../..").Path
$ids = @(0) + @(37..43) + @(46..54) + @(192..196)
for($cell=1;$cell -lt $ids.Count;$cell++) {
 $id=$ids[$cell]
 [ToneImport]::Frame($ImagePath,"$repo/build/pc/tone_fix_backup/jesus_ferido_$id.png","$repo/assets/ric/jesus_ferido/jesus_ferido_$id.png",$cell)
}
