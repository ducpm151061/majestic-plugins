1.当前文件夹说明:
 a. enc_rgb:跑加密算法模型，并且算法模型输入数据格式为argb8888，所需要的配置和文件说明
 b. rgb：跑开源算法模型，并且算法模型输入数据格式为argb8888，所需要的配置和文件说明
 c. yuv:算法模型，并且算法模型输入数据格式为yuv420，所需要的配置和文件说明
  
2. 加密模型，主要通过在模型名字前面加enc判断是否是加密模型

a. fd执行命令

  ./prog_dla_fdfr_vpe sstar_param_fd_enc.ini
  
  
b. fdfr执行命令

  ./prog_dla_fdfr_vpe sstar_param_fdfr_enc.ini
  
  
2. 没有加密模型(rgb)

a. fd执行命令

  ./prog_dla_fdfr_vpe sstar_param_fd.ini
  
  
b. fdfr执行命令

  ./prog_dla_fdfr_vpe sstar_param_fdfr.ini
  
  
3. 没有加密模型(yuv)

a. fd执行命令

  ./prog_dla_fdfr_vpe sstar_param_fd_yuv.ini
  
  
b. fdfr执行命令

  ./prog_dla_fdfr_vpe sstar_param_fdfr_yuv.ini
  
4. fdfr人脸特征添加/删除

  a. 输入12进入添加或删除模式
  b. 输入a然后按照提示添加特征
  c. 输入d然后按照提示添加特征
  
  
5. .ini配置注意事项

   执行前需要主要查看配置中模型的路径是否正确