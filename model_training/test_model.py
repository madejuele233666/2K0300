import os
import random
import numpy as np
import tensorflow as tf

def load_image(image_path, target_size=(224, 224)):
    """加载并转换图像为网络可用的 NumPy 数组 (1, 224, 224, 3)"""
    img = tf.keras.utils.load_img(image_path, target_size=target_size)
    img_array = tf.keras.utils.img_to_array(img)
    # 增加 batch 维度: (1, 224, 224, 3)
    img_array = np.expand_dims(img_array, axis=0)
    return img_array

def test_keras_model(model_path, class_names, image_array):
    print(f"\n[Keras Model] {os.path.basename(model_path)}")
    model = tf.keras.models.load_model(model_path)
    
    predictions = model.predict(image_array, verbose=0)
    predicted_idx = np.argmax(predictions[0])
    confidence = predictions[0][predicted_idx]
    
    print(f" => Predicted: {class_names[predicted_idx]} (Confidence: {confidence:.2%})")

def test_tflite_model(tflite_path, class_names, image_array):
    print(f"\n[TFLite Model] {os.path.basename(tflite_path)}")
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    # 根据模型设定转换输入类型 (对于默认优化通常依然是 float32)
    input_type = input_details['dtype']
    
    # 针对可能存在的全量化模型作准备
    if input_type == np.uint8 or input_type == np.int8:
        scale, zero_point = input_details['quantization']
        if scale > 0:
            input_data = (image_array / scale + zero_point).astype(input_type)
        else:
            input_data = image_array.astype(input_type)
    else:
        input_data = image_array.astype(input_type)

    # 推理
    interpreter.set_tensor(input_details['index'], input_data)
    interpreter.invoke()
    output_data = interpreter.get_tensor(output_details['index'])
    
    predicted_idx = np.argmax(output_data[0])
    
    # 粗略置信度
    output_type = output_details['dtype']
    if output_type != np.float32 and output_type != np.float16:
        scale, zero_point = output_details['quantization']
        if scale > 0:
            confidence = (output_data[0][predicted_idx] - zero_point) * scale
        else:
            # 如果没有正确的量化参数，尝试直接用最大值作为参考
            confidence = output_data[0][predicted_idx] / 255.0
    else:
        confidence = output_data[0][predicted_idx]
        
    print(f" => Predicted: {class_names[predicted_idx]} (Confidence: {confidence:.2%})")

if __name__ == '__main__':
    # 相关文件或目录路径
    dataset_dir = 'dataset'
    class_names = ['supplies', 'vehicle', 'weapon']
    
    keras_model_path = 'saved_models/mobilenet_v3_small_model.keras'
    tflite_model_path = 'saved_models/mobilenet_v3_small.tflite'
    tflite_quantized_path = 'saved_models/mobilenet_v3_small_quantized.tflite'

    # 随机挑选一张测试图片
    classes = [d for d in os.listdir(dataset_dir) if os.path.isdir(os.path.join(dataset_dir, d))]
    true_class = random.choice(classes)
    class_path = os.path.join(dataset_dir, true_class)
    
    images = [img for img in os.listdir(class_path) if img.lower().endswith(('.png', '.jpg', '.jpeg'))]
    if not images:
         print(f"No images found in {class_path}")
         exit(1)
         
    test_image_file = random.choice(images)
    test_image_path = os.path.join(class_path, test_image_file)
    
    print("=" * 60)
    print(f" Testing an Image")
    print(f" Image Path : {test_image_path}")
    print(f" True Class : {true_class}")
    print("=" * 60)
    
    # 预加载图片数据
    image_array = load_image(test_image_path)
    
    # 依次测试所有格式的模型
    test_keras_model(keras_model_path, class_names, image_array)
    test_tflite_model(tflite_model_path, class_names, image_array)
    test_tflite_model(tflite_quantized_path, class_names, image_array)
    
    print("\nTest completed.")
