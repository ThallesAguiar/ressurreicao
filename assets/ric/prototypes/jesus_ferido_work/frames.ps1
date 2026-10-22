param([ValidateSet('guides','export','finish')][string]$Action,[int]$Batch=0,[string]$ImagePath)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class FrameArt {
 static Color[] Colors() {
  int[] rgb={0x211716,0x493023,0x704932,0x9a6745,0xc48e5d,0xf1bb86,0xffefd0,0xe2d4b4,0xb7aa91,0x8b8276,0x5d5955,0x42191b,0x7e2425,0xb44637,0xc6c8c1};
  Color[] c=new Color[16]; c[0]=Color.Transparent; for(int i=0;i<15;i++)c[i+1]=Color.FromArgb(255,(rgb[i]>>16)&255,(rgb[i]>>8)&255,rgb[i]&255);return c;
 }
 public static Rectangle Bounds(Bitmap im,Rectangle area,int threshold) {
  int l=area.Right,t=area.Bottom,r=area.Left,b=area.Top;
  for(int y=area.Top;y<area.Bottom;y++)for(int x=area.Left;x<area.Right;x++)if(im.GetPixel(x,y).A>=threshold){l=Math.Min(l,x);t=Math.Min(t,y);r=Math.Max(r,x+1);b=Math.Max(b,y+1);}
  return r>l&&b>t?Rectangle.FromLTRB(l,t,r,b):Rectangle.Empty;
 }
 public static void Guide(string[] files,int[] ids,string path) {
  using(var im=new Bitmap(1536,1536,PixelFormat.Format32bppArgb)) {
   using(var g=Graphics.FromImage(im)) {g.Clear(Color.Transparent);using(var pen=new Pen(Color.FromArgb(255,64,64,70),4))using(var font=new Font("Consolas",22)) {
    for(int i=0;i<16;i++){int ox=(i%4)*384,oy=(i/4)*384;g.DrawRectangle(pen,ox+2,oy+2,380,380);if(i<files.Length)g.DrawString(ids[i].ToString(),font,Brushes.Gray,ox+12,oy+6);}
   }}
   for(int i=0;i<files.Length;i++)using(var src=new Bitmap(files[i])) {
    int ox=(i%4)*384+(96-src.Width)/2*4,oy=(i/4)*384+(12+(76-src.Height)/2)*4;
    for(int y=0;y<src.Height;y++)for(int x=0;x<src.Width;x++){Color c=src.GetPixel(x,y);if(c.A<200)continue;for(int dy=0;dy<4;dy++)for(int dx=0;dx<4;dx++)im.SetPixel(ox+x*4+dx,oy+y*4+dy,Color.FromArgb(255,c.R,c.G,c.B));}
   }
   im.Save(path,ImageFormat.Png);
  }
 }
 public static void Export(string sheet,string source,string output,int cell) {
  using(var im=new Bitmap(sheet))using(var src=new Bitmap(source)) {
   int cl=(cell%4)*im.Width/4,ct=(cell/4)*im.Height/4,cw=im.Width/4,ch=im.Height/4;
   Rectangle area=new Rectangle(cl+cw*8/96,ct+ch*13/96,cw*80/96,ch*70/96);
   Rectangle art=Bounds(im,area,180),target=Bounds(src,new Rectangle(0,0,src.Width,src.Height),200);
   if(art.IsEmpty||target.IsEmpty)throw new Exception("Empty frame "+cell);
   Color[] colors=Colors();int id=int.Parse(System.IO.Path.GetFileNameWithoutExtension(source).Replace("richter_",""));
   if(id==112||id==165||id==166||(id>=169&&id<=178)) {var original=src.Palette.Entries;for(int i=1;i<16&&i<original.Length;i++)colors[i]=Color.FromArgb(255,original[i].R,original[i].G,original[i].B);}
   using(var dst=new Bitmap(src.Width,src.Height,PixelFormat.Format8bppIndexed)) {
    var pal=dst.Palette;for(int i=0;i<pal.Entries.Length;i++)pal.Entries[i]=i<16?colors[i]:colors[0];dst.Palette=pal;
    var data=dst.LockBits(new Rectangle(0,0,dst.Width,dst.Height),ImageLockMode.WriteOnly,PixelFormat.Format8bppIndexed);byte[] raw=new byte[data.Stride*dst.Height];
    for(int y=0;y<target.Height;y++)for(int x=0;x<target.Width;x++) {
     int sx=Math.Min(art.Right-1,art.Left+(int)((x+0.5)*art.Width/target.Width)),sy=Math.Min(art.Bottom-1,art.Top+(int)((y+0.5)*art.Height/target.Height));Color c=im.GetPixel(sx,sy);if(c.A<180)continue;
     int best=1;double distance=double.MaxValue;for(int k=1;k<16;k++){double dr=c.R-colors[k].R,dg=c.G-colors[k].G,db=c.B-colors[k].B,d=dr*dr+dg*dg+db*db;if(d<distance){distance=d;best=k;}}
     raw[(y+target.Top)*data.Stride+x+target.Left]=(byte)best;
    }
    Marshal.Copy(raw,0,data.Scan0,raw.Length);dst.UnlockBits(data);dst.Save(output,ImageFormat.Png);
   }
  }
 }
 public static void Contact(string[] files,int[] ids,string path) {
  int cols=10,rows=(files.Length+cols-1)/cols;
  using(var im=new Bitmap(cols*144,rows*160))using(var g=Graphics.FromImage(im))using(var font=new Font("Consolas",10)) {
   g.Clear(Color.FromArgb(32,34,42));
   for(int i=0;i<files.Length;i++)using(var src=new Bitmap(files[i])) {
    int ox=(i%cols)*144+(144-src.Width*2)/2,oy=(i/cols)*160+8;
    for(int y=0;y<src.Height;y++)for(int x=0;x<src.Width;x++){Color c=src.GetPixel(x,y);if(c.A<128)continue;using(var brush=new SolidBrush(c))g.FillRectangle(brush,ox+x*2,oy+y*2,2,2);}
    g.DrawString(ids[i].ToString(),font,Brushes.White,(i%cols)*144+4,(i/cols)*160+142);
   }im.Save(path,ImageFormat.Png);
  }
 }
}
'@
$manifest=Get-Content -Raw -LiteralPath "$PSScriptRoot/manifest.json" | ConvertFrom-Json
if($Action -eq 'guides') {
 for($i=0;$i -lt $manifest.batches.Count;$i++) {
  $frames=$manifest.batches[$i]
  [FrameArt]::Guide([string[]]@($frames|ForEach-Object{$_.source}),[int[]]@($frames|ForEach-Object{$_.index}),"$PSScriptRoot/guide_$('{0:D2}' -f $i).png")
 }
} elseif($Action -eq 'export') {
 $frames=$manifest.batches[$Batch]
 for($i=0;$i -lt $frames.Count;$i++){[FrameArt]::Export($ImagePath,$frames[$i].source,$frames[$i].output,$i)}
} elseif($Action -eq 'finish') {
 foreach($f in $manifest.frames){if($f.master -ne $f.index){Copy-Item -LiteralPath $manifest.frames[$f.master].output -Destination $f.output}}
 [FrameArt]::Contact([string[]]@($manifest.frames|ForEach-Object{$_.output}),[int[]]@($manifest.frames|ForEach-Object{$_.index}),"$PSScriptRoot/../jesus_ferido_contact.png")
}
