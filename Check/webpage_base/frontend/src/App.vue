<template>
  <div class="page">

    <h1>
      工业视觉检测系统
    </h1>


    <div class="container">

      <div class="card">

        <h2>
          原始图片
        </h2>

        <div class="image-box">
          图片区域
        </div>

      </div>



      <div class="card">

        <h2>
          检测结果图片
        </h2>

        <div class="image-box">
          缺陷框图片区域
        </div>

      </div>


    </div>



    <div class="result">

      <h2>
        检测结果
      </h2>


      <p>
        状态:
        NG
      </p>


      <p>
        缺陷数量:
        2
      </p>


    </div>


  </div>
</template>



<style scoped>

.page
{
  padding:40px;
}


.container
{
  display:flex;
  gap:30px;
}


.card
{
  border:1px solid #ccc;
  padding:20px;
  width:45%;
}


.image-box
{
  height:300px;
  background:#eee;

  display:flex;
  justify-content:center;
  align-items:center;
}


.result
{
  margin-top:30px;

  border:1px solid #ccc;

  padding:20px;
}


</style>