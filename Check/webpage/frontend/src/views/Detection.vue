<template>

<div class="page">


<h1>
工业视觉检测系统
</h1>


<div class="image-container">


<div class="image-card">

<h2>
原始图片
</h2>


<img 
src="/images/original.jpg"
class="image"
/>


</div>



<div class="image-card">

<h2>
检测结果图片
</h2>


<img 
src="/images/result.jpg"
class="image"
/>


</div>


</div>


</div>

</template>



<style scoped>


.page
{
padding:40px;
}


.image-container
{
display:flex;

gap:30px;

}



.image-card
{
width:45%;

border:1px solid #ddd;

padding:20px;

}



.image
{

width:100%;

height:400px;

object-fit:contain;

background:#eee;

}


</style>