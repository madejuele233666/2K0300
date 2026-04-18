import cv2
import numpy as np
import tensorflow as tf
import time

def load_tflite_model(model_path):
    interpreter = tf.lite.Interpreter(model_path=model_path)
    interpreter.allocate_tensors()
    return interpreter

def predict_frame(interpreter, frame, class_names):
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    # OpenCV 的图片格式是 BGR，但 Keras 训练时一般输入为 RGB，因此必须转换
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    
    # 调整长宽尺寸为模型输入尺寸 224x224
    resized_frame = cv2.resize(rgb_frame, (224, 224))
    
    # 增加 batch 维度: (1, 224, 224, 3)
    input_data = np.expand_dims(resized_frame, axis=0)

    input_type = input_details['dtype']
    if input_type == np.uint8 or input_type == np.int8:
        scale, zero_point = input_details['quantization']
        if scale > 0:
            input_data = (input_data / scale + zero_point).astype(input_type)
        else:
            input_data = input_data.astype(input_type)
    else:
        input_data = input_data.astype(input_type)

    # 送入模型进行推理
    interpreter.set_tensor(input_details['index'], input_data)
    interpreter.invoke()
    
    output_data = interpreter.get_tensor(output_details['index'])
    predicted_idx = np.argmax(output_data[0])
    
    # 获取置信度
    output_type = output_details['dtype']
    if output_type != np.float32 and output_type != np.float16:
        scale, zero_point = output_details['quantization']
        if scale > 0:
            confidence = (output_data[0][predicted_idx] - zero_point) * scale
        else:
            confidence = output_data[0][predicted_idx] / 255.0
    else:
        confidence = output_data[0][predicted_idx]
        
    return class_names[predicted_idx], confidence

def main():
    class_names = ['supplies', 'vehicle', 'weapon']
    # 这里我们采用体积最小且推理速度最快的量化版 TFLite 模型进行实时测试
    tflite_path = 'saved_models/mobilenet_v3_small_quantized.tflite'
    
    print("Loading TFLite model...")
    interpreter = load_tflite_model(tflite_path)
    
    print("Opening webcam...")
    # 0 代表默认自带摄像头，如果你有外接 USB 摄像头，有时候可能是 1 或者 2
    cap = cv2.VideoCapture(0)
    
    if not cap.isOpened():
        print("Error: Could not open webcam.")
        return

    print("Webcam started. Press 'q' to quit.")
    
    while True:
        ret, frame = cap.read()
        if not ret:
            print("Failed to grab frame.")
            break
            
        start_time = time.time()
        
        # 为了更好地保留主体，进行居中裁剪成一个正方形，当然这不是必须的，也可以直接把长方形形变 resize
        h, w, _ = frame.shape
        min_dim = min(h, w)
        start_x = (w - min_dim) // 2
        start_y = (h - min_dim) // 2
        cropped_frame = frame[start_y:start_y+min_dim, start_x:start_x+min_dim]
        
        # 预测
        predicted_class, confidence = predict_frame(interpreter, cropped_frame, class_names)
        
        fps = 1.0 / (time.time() - start_time)
        
        # 将结果写在原图上
        text1 = f"Class: {predicted_class} ({confidence:.1%})"
        text2 = f"FPS: {fps:.1f}"
        
        cv2.putText(frame, text1, (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2, cv2.LINE_AA)
        cv2.putText(frame, text2, (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 0), 2, cv2.LINE_AA)
        
        # 弹窗显示画面
        cv2.imshow("Webcam Inference Test", frame)
        
        # 按 'q' 键退出循环
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
            
    # 释放资源
    cap.release()
    cv2.destroyAllWindows()

if __name__ == '__main__':
    main()
